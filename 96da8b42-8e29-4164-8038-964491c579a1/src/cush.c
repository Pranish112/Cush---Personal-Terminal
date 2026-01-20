/*
 * cush - the customizable shell.
 *
 */
#define _GNU_SOURCE    1
#include <stdio.h>
#include <readline/readline.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <sys/wait.h>
#include <assert.h>
#include <spawn.h>
#include <errno.h>
#include <fcntl.h>

/* Since the handed out code contains a number of unused functions. */
#pragma GCC diagnostic ignored "-Wunused-function"

#include "termstate_management.h"
#include "signal_support.h"
#include "shell-ast.h"
#include "utils.h"

extern char **environ;

/* Variable info for history - complec built-in function*/
#define MAX_HISTORY 1000
static char *history[MAX_HISTORY];
static int history_count = 0;

static void handle_child_status(pid_t pid, int status);

static void
usage(char *progname)
{
    printf("Usage: %s -h\n"
           " -h            print this help\n",
           progname);
    exit(EXIT_SUCCESS);
}

/* Build a prompt */
static char *
build_prompt(void)
{
    return strdup("cush> ");
}

enum job_status {
    FOREGROUND,     /* job is running in foreground.  Only one job can be
                       in the foreground state. */
    BACKGROUND,     /* job is running in background */
    STOPPED,        /* job is stopped via SIGSTOP */
    NEEDSTERMINAL,  /* job is stopped because it was a background job
                       and requires exclusive terminal access */
};

struct job {
    struct list_elem elem;        /* Link element for jobs list. */
    struct ast_pipeline *pipe;    /* The pipeline of commands this job represents */
    int     jid;                  /* Job id. */
    enum job_status status;       /* Job status. */
    int     num_processes_alive;  /* The number of processes that we know to be alive */
    struct termios saved_tty_state;  /* The state of the terminal if job is stopped */
    pid_t pgid;                       /* For a single-command pipeline, store child’s pid. */
    pid_t *pids;                    /* Array to store the pids*/
    int num_pids;                   /*Total number of pids*/
};

#define MAXJOBS (1<<16)
static struct list job_list;
static struct job * jid2job[MAXJOBS];

/* Return job corresponding to jid */
static struct job * 
get_job_from_jid(int jid)
{
    if (jid > 0 && jid < MAXJOBS && jid2job[jid] != NULL)
        return jid2job[jid];
    return NULL;
}

/* Add a new job to the job list */
static struct job *
add_job(struct ast_pipeline *pipe)
{
    struct job *job = malloc(sizeof(struct job));
    job->pipe = pipe;
    job->num_processes_alive = 0;
    job->pids = malloc(sizeof(pid_t) * 100);
    job->num_pids = 0;
    list_push_back(&job_list, &job->elem);
    for (int i = 1; i < MAXJOBS; i++) {
        if (jid2job[i] == NULL) {
            jid2job[i] = job;
            job->jid = i;
            return job;
        }
    }
    fprintf(stderr, "Maximum number of jobs exceeded\n");
    abort();
    return NULL;
}

/* Delete a job.
 * This should be called only when all processes that were
 * forked for this job are known to have terminated.
 */
static void
delete_job(struct job *job)
{
    if (job->jid == -1)
        return;
    
    list_remove(&job->elem);
    int jid = job->jid;
    jid2job[jid]->jid = -1;
    jid2job[jid] = NULL;
    ast_pipeline_free(job->pipe);
    free(job);
}

static const char *
get_status(enum job_status status)
{
    switch (status) {
    case FOREGROUND:   return "Foreground";
    case BACKGROUND:   return "Running";
    case STOPPED:      return "Stopped";
    case NEEDSTERMINAL:return "Stopped (tty)";
    default:           return "Unknown";
    }
}

/* Print the command line that belongs to one job. */
static void
print_cmdline(struct ast_pipeline *pipeline)
{
    struct list_elem * e = list_begin (&pipeline->commands); 
    for (; e != list_end (&pipeline->commands); e = list_next(e)) {
        struct ast_command *cmd = list_entry(e, struct ast_command, elem);
        if (e != list_begin(&pipeline->commands))
            printf("| ");
        char **p = cmd->argv;
        printf("%s", *p++);
        while (*p)
            printf(" %s", *p++);
        //printf("");
    }
}

// Helper method to print the background process to the foreground, used in cmd_fg
static void
print_fg(struct ast_pipeline *pipeline)
{
    struct list_elem * e = list_begin (&pipeline->commands); 

    for (; e != list_end (&pipeline->commands); e = list_next (e)) {
        struct ast_command *cmd = list_entry(e, struct ast_command, elem);

        char **parts = cmd->argv;
        while (*parts) {
            printf("%s", *parts);
            fflush(stdout);
            parts++;
            if(*parts != NULL)
                printf(" ");
        }

        if (list_size(&pipeline->commands) > 1) 
        {
            printf(" | ");
        }
    }

    printf("\n");
}


static void
print_job(struct job *job)
{
    printf("[%d]   %s         (", job->jid, get_status(job->status));
    print_cmdline(job->pipe);
    printf(")\n");
}


static void
sigchld_handler(int sig, siginfo_t *info, void *_ctxt)
{
    pid_t child;
    int status;

    assert(sig == SIGCHLD);

    while ((child = waitpid(-1, &status, WUNTRACED|WNOHANG)) > 0) 
    {
        handle_child_status(child, status);
    }
}

/*
 * Wait for all processes in this job to complete, or for
 * the job no longer to be in the foreground.
 */
static void
wait_for_job(struct job *job)
{
    assert(signal_is_blocked(SIGCHLD));

    while (job->status == FOREGROUND && job->num_processes_alive > 0) 
    {
        int status;
        pid_t child = waitpid(-1, &status, WUNTRACED);
        if (child != -1) 
        {
            handle_child_status(child, status);
        } 
        else 
        {
            utils_fatal_error("waitpid failed, see code for explanation");
        }
    }
}

/*
 * Handle the SIGCHLD result for a single pid. Adjust job fields accordingly.
 */
static void
handle_child_status(pid_t pid, int status)
{
    assert(signal_is_blocked(SIGCHLD));

    // Find which job this PID belongs to
    struct job *job = NULL;
    for (struct list_elem *e = list_begin(&job_list); e != list_end(&job_list); e = list_next(e)) 
    {
        struct job *j = list_entry(e, struct job, elem);
        for (int i = 0; i < j->num_pids; i++)
        {
            if (j->pids[i] == pid) 
            {
                job = j;
                break;
            }
        }
    }
    if (!job)
    {
        printf("no jobs found");
        return;
    }
       
    /* Evaluates the status as necessary based on signal calls. */
    if (WIFEXITED(status) || WIFSIGNALED(status)) 
    {
        if (WIFSIGNALED(status)) 
        {
            
            printf("%s\n", strsignal(WTERMSIG(status)));
            job->status = BACKGROUND;
        }
        job->num_processes_alive--;
    }
    else if (WIFSTOPPED(status)) 
    {
        //enum job_status old_status = job->status;
        job->status = STOPPED;

        // /* Save the terminal state. */
        // if (old_status == FOREGROUND) 
        // {
        //     termstate_save(&job->saved_tty_state);
        //     termstate_give_terminal_back_to_shell();
        // }
        printf("\n");
        print_job(job);
        termstate_save(&job->saved_tty_state);
        termstate_give_terminal_back_to_shell();
    }
    else if (WIFCONTINUED(status)) 
    {
        job->status = BACKGROUND; 
    }
}


static void
cmd_jobs(void)
{
    struct list_elem *e;
    for (e = list_begin(&job_list); e != list_end(&job_list); e = list_next(e)) 
    {
        struct job *job = list_entry(e, struct job, elem);
        
        if (job->status == BACKGROUND || job->status == FOREGROUND || job->status == STOPPED)
        {
            print_job(job);
        }
    }

    //termstate_give_terminal_back_to_shell();
}

static void
cmd_fg(int jid)
{
    signal_block(SIGCHLD);
    struct job *job = get_job_from_jid(jid);

    if (job == NULL) 
    {
        printf("fg: Job %d not found\n", jid);
        signal_unblock(SIGCHLD);
        return;
    }

    print_fg(job->pipe);

    job->status = FOREGROUND;

    termstate_give_terminal_to(NULL, job->pgid);
    killpg(job->pgid, SIGCONT);

    wait_for_job(job);
    
    signal_unblock(SIGCHLD);
    termstate_give_terminal_back_to_shell();
}


static void
cmd_bg(int jid)
{
    struct job *job = get_job_from_jid(jid);
    if (job == NULL) {
        fprintf(stderr, "bg: Job %d not found\n", jid);
        return;
    }

    if (job->status == STOPPED || job->status == NEEDSTERMINAL)
     {
        job->status = BACKGROUND;
        killpg(job->pgid, SIGCONT);
        printf("[%d] %d\n", job->jid, job->pgid);
    } 
    else 
    {
        fprintf(stderr, "bg: job %d is not stopped.\n", jid);
    }

}

static void
cmd_kill(int jid)
{
    struct job *job = get_job_from_jid(jid);
    if (!job) 
    {
        fprintf(stderr, "kill: Job %d not found\n", jid);
        return;
    }
    killpg(job->pgid, SIGTERM);

    termstate_give_terminal_back_to_shell();
}

static void
cmd_stop(int jid)
{
    struct job *job = get_job_from_jid(jid);
    if (job == NULL) {
        fprintf(stderr, "stop: Job %d not found\n", jid);
        return;
    }
    
    if (killpg(job->pgid, SIGSTOP) != 0) {
        perror("killpg");
    }
}

static void do_pipeline(struct ast_pipeline *pipeline)
{
    signal_block(SIGCHLD);
    bool usePipe = false;
    int num_cmds = list_size(&pipeline->commands);
    int *pipe_file_ds = NULL;

    if (num_cmds > 1)
    {
        pipe_file_ds = (int *)malloc(2 * (num_cmds - 1) * sizeof(int));
        if (pipe_file_ds == NULL)
        {
            perror("malloc");
            exit(EXIT_FAILURE);
        }
        for (int i = 0; i < num_cmds - 1; i++)
        {
            if (pipe(pipe_file_ds + i * 2) == -1)
            {
                perror("pipe");
                exit(EXIT_FAILURE);
            }
        }

        usePipe = true;

    }

    struct job *job = add_job(pipeline);
    job->pids = (pid_t*)malloc(num_cmds * sizeof(pid_t));
    if (job->pids == NULL)
    {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    job->num_processes_alive = 0;
    job->num_pids = 0;

    if (pipeline->bg_job == true) 
    {
        job->status = BACKGROUND;
    } 
    else 
    {
        job->status = FOREGROUND;
    }

    int i = 0;
    for (struct list_elem *e = list_begin(&pipeline->commands); e != list_end(&pipeline->commands); e = list_next(e))
    {
        struct ast_command *cmd = list_entry(e, struct ast_command, elem);

        pid_t pid;
        posix_spawnattr_t attr;
        posix_spawn_file_actions_t file_attr;

        posix_spawnattr_init(&attr);
        posix_spawn_file_actions_init(&file_attr);
        posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);

        if (!pipeline->bg_job)
        {
            unsigned short flags = POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_TCSETPGROUP;
            posix_spawnattr_setflags(&attr, flags);
            posix_spawnattr_tcsetpgrp_np(&attr, termstate_get_tty_fd());

        }

        if (i == 0)
        {
            posix_spawnattr_setpgroup(&attr, 0);
        }
        else
        {
            posix_spawnattr_setpgroup(&attr, job->pgid);
        }

        // For I/O redirection
        mode_t mode =  S_IWUSR | S_IRGRP | S_IRUSR | S_IROTH;

        if (pipeline->iored_input != NULL && i == 0)
        {
            posix_spawn_file_actions_addopen(&file_attr, STDIN_FILENO, pipeline->iored_input, O_RDONLY, mode);
        }

        if (i == num_cmds - 1 && pipeline->iored_output != NULL)
        {
            int flags = O_WRONLY | O_CREAT;
            if (pipeline->append_to_output) 
            {
                flags |= O_APPEND;
            } 
            else 
            {
                flags |= O_TRUNC;
            }

            posix_spawn_file_actions_addopen(&file_attr, STDOUT_FILENO, pipeline->iored_output, flags, mode);
        }

        if (cmd->dup_stderr_to_stdout)
        {
            posix_spawn_file_actions_adddup2(&file_attr, STDOUT_FILENO, STDERR_FILENO);
        }

        if (usePipe)
        {
            if (i > 0)
            {
                posix_spawn_file_actions_adddup2(&file_attr, pipe_file_ds[(i - 1) * 2], STDIN_FILENO);
            }
            if ( i < num_cmds - 1)
            {
                int val = i * 2 + 1;
                posix_spawn_file_actions_adddup2(&file_attr, pipe_file_ds[val], STDOUT_FILENO);
                if (cmd->dup_stderr_to_stdout)
                {
                    posix_spawn_file_actions_adddup2(&file_attr, pipe_file_ds[val], STDERR_FILENO);
                }
            }

            for (int j = 0; j < 2 * (num_cmds - 1); j++)
            {
                posix_spawn_file_actions_addclose(&file_attr, pipe_file_ds[j]);
            }
        }

        if (posix_spawnp(&pid, cmd->argv[0], &file_attr, &attr, cmd->argv, environ) == 0)
        {
            if (i == 0)
            {
                job->pgid = pid;
            }
            job->pids[job->num_pids++] = pid;
            
            job->num_processes_alive++;
        }
        else
        {
            printf("no such file or directory");
        }

        posix_spawn_file_actions_destroy(&file_attr);
        posix_spawnattr_destroy(&attr); 
        
        i++;

    }


    if (usePipe)
    {
        for (int i = 0; i < 2 * (num_cmds - 1); i++)
        {
            close(pipe_file_ds[i]);
        }
        free(pipe_file_ds);
    }

    if (!pipeline->bg_job) 
    {
        //signal_block(SIGCHLD);
        //termstate_save(&job->saved_tty_state);
        wait_for_job(job);
        termstate_give_terminal_back_to_shell();
    
    }
    else
    {
        printf("[%d] %d\n", job->jid, job->pgid);
    }

    if (job->num_processes_alive == 0)
    {
        delete_job(job);
    }
    
    signal_unblock(SIGCHLD);
}

// Simple built in, host name and info
static char *getHostName(void) 
{
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0) 
    {
        perror("gethostname");
        return NULL;
    }
    
    char *result = strdup(hostname);
    if (result == NULL) 
    {
        perror("strdup");
    }
    return result;
}

// Complex built in history - which gives you all the history of all commands that have been run 
static void
cmd_history(void)
{
    // Print each saved command with its number.
    for (int i = 0; i < history_count; i++) {
        printf("%d: %s\n", i + 1, history[i]);
    }
}

static bool builtIn(struct ast_command_line *cmdElem)
{
    if (cmdElem == NULL || list_empty(&cmdElem->pipes))
    {
        return false;
    }

    struct ast_pipeline *pipe = list_entry(list_begin(&cmdElem->pipes), struct ast_pipeline, elem);

    if (list_empty(&pipe->commands))
    {
        return false;
    }

    struct ast_command *cmd = list_entry(list_begin(&pipe->commands), struct ast_command, elem);

    if (strcmp(cmd->argv[0], "fg") == 0) 
    {
        if (cmd->argv[1] == NULL)
        {
            return false;
        }

        int jid = atoi(cmd->argv[1]);
        cmd_fg(jid);
        return true;
    } 
    else if (strcmp(cmd->argv[0], "bg") == 0) 
    {
        if (cmd->argv[1] == NULL)
        {
            return false;
        }
        int jid = atoi(cmd->argv[1]);
        cmd_bg(jid);
        return true;
    } 
    else if (strcmp(cmd->argv[0], "kill") == 0) 
    {
        if (cmd->argv[1] == NULL)
        {
            return false;
        }
        int jid = atoi(cmd->argv[1]);
        cmd_kill(jid);
        return true;
    }
    else if (strcmp(cmd->argv[0], "jobs") == 0) 
    {
        cmd_jobs();
        return true;
    }
    else if (strcmp(cmd->argv[0], "exit") == 0) 
    {
        exit(EXIT_SUCCESS);
        return true;
    }
    else if (strcmp(cmd->argv[0], "stop") == 0) 
    {
        int jid = atoi(cmd->argv[1]);
        cmd_stop(jid);
        return true;
    }
    else if (strcmp(cmd->argv[0], "info") == 0)
    {
        char hostname[256];
        if (gethostname(hostname, sizeof(hostname)) != 0) {
            perror("gethostname");
        }
        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd)) == NULL) {
            perror("getcwd");
        }
        printf("Hostname: %s\n", hostname);
        printf("Current Directory: %s\n", cwd);
        return true;
    }
    else if (strcmp(cmd->argv[0], "history") == 0)
    {
        cmd_history();
        return true;
    }

    return false;
}

//cleans up all the jobs
static void cleanUp()
{
    struct list_elem *e = list_begin(&job_list);

    while (e != list_end(&job_list))
    {
        struct job *job = list_entry(e, struct job, elem);
        struct list_elem *next = list_next(e);
        
        if (job->num_processes_alive == 0)
        {
            delete_job(job);
        }

        e = next;
    }
}

int
main(int ac, char *av[])
{
    int opt;

    /* Process command-line arguments. See getopt(3) */
    while ((opt = getopt(ac, av, "h")) > 0) {
        switch (opt) {
        case 'h':
            usage(av[0]);
            break;
        }
    }

    list_init(&job_list);
    signal_set_handler(SIGCHLD, sigchld_handler);
    termstate_init();
    // signal(SIGINT, SIG_IGN);

    /* Read/eval loop. */
    for (;;) 
    {
        assert(!signal_is_blocked(SIGCHLD));
        assert(termstate_get_current_terminal_owner() == getpgrp());

        /* Only prompt if stdin is a terminal */
        cleanUp();
        char * prompt = isatty(STDIN_FILENO) ? build_prompt() : NULL;
        char * cmdline = readline(prompt);
        free(prompt);

        if (cmdline == NULL)  /* User typed EOF */
            break;

        // Stores the history of commands that are being run on the terminal
        if (cmdline[0] != '\0' && history_count < MAX_HISTORY) 
        {
            history[history_count] = strdup(cmdline);
            if (history[history_count] == NULL) {
                perror("strdup");
                exit(EXIT_FAILURE);
            }
            history_count++;
        }

        struct ast_command_line * cline = ast_parse_command_line(cmdline);

        if (cline == NULL) 
        {
            free(cmdline);
            continue;
        }

        if (list_empty(&cline->pipes))
        { 
            ast_command_line_free(cline);
            free(cmdline);
            continue;
        } 

        /* Checks for built-in commands first, and then runs the pipeline */
        if (!builtIn(cline))
        {
            for (struct list_elem *e = list_begin(&cline->pipes); e != list_end(&cline->pipes); e = list_next(e))
            {
                struct ast_pipeline *pipeline = list_entry(e, struct ast_pipeline, elem);
                do_pipeline(pipeline);

            }
        }

        free(cmdline);
        if (cline == NULL)
        {
            continue;
        }

        if (list_empty(&cline->pipes))
        { 
            ast_command_line_free(cline);
            continue;
        } 
    }
    return 0;
}
