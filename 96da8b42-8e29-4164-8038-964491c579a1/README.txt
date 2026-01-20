Student Information
-------------------
Pranish Somyajula - psomyaju@vt.edu

How to execute the shell
------------------------
To execute the cush.
STEPS:
    1. Go into the src folder
    2. run the make command
    3. run ./cush
    4. This should open up the cush terminal to run the commands.


Important Notes
---------------
The system works just fine, how ever as per implementation, here are the key notes.
1. The main calls the boolean builtIn method, which checks if the command is a built-in command
    or it is a pipeline executed command.
2. If the boolean fails, it calls the do_pipeline method, which runs the pipelines as necessary.
3. The code is easy to understand. but did make a few changes in the already provided delete_job and add_job methods
4. I also had a clean up method, which cleans up the list and number_of_alive_processes, before each iteration of the for (;;)
    loop.


Description of Base Functionality
---------------------------------
<describe your IMPLEMENTATION of the following commands:
jobs, fg, bg, kill, stop, \ˆC, \ˆZ >

JOBS:
    For this implementation, all I did was loop through the job_lists, and pick the first entry,
    and if the status of that job was FOREGROUND, BACKGROUND, or STOPPED, I would print it using the print_job function.

FG:
    For this implementation, I got the job from the jid, and then using the helper method print_fg, which prints the BACKGROUND
     status process to the terminal, then I change my job status to FOREGROUND, kill any process groups, and then using the signal
     block and unblock, call wait_for_jobs, to finish the process that has been brought to the foreground.

BG:
    For this implementation, I got the job from the jid, and then if the job status was NEEDSTERMINAL or STOPPED, i changed its job status
    to BACKGROUND, and then killed any process groups, and printed the job to the terminal using the necessary jid.

KILL:
    For this implementation, I got the job from the jid, and then killed the process group, using SIGTERM signal call associated with that jid, and then
    gave the terminal control back to the shell.

STOP:
    For this implementation, I got the job from the jid, and then killed the process group, using the SIGSTOP signal call associated to the jid.


Description of Extended Functionality
-------------------------------------
<describe your IMPLEMENTATION of the following functionality:
I/O, Pipes, Exclusive Access >

For the implementation of the pipeline, the first loop runs through in the main after the builtin check, then once you are inside the do_pipeline
method, it checks if the num of commands are greather than 1, if so it makes the boolean use pipe true,
once usePipe is true, then it goes into the second for loop needed for piping, where it checks which iteration the loop is in, and before
any functionality, it intializes all the posix_spawn variables an file_actions and attributes, and also sets the necessary flags.

if the process started at the start of the pipe, then I utilized the iored_input to I/O redirect the file as an input,
and if the process started from the end, I did iored_output, and set up the flags accordingly.

and then if the cmd has a dup_stderr_to_stdout then I called the posix_spawn_adddup2 function, to pipe the values through file file_actions
properly. and if the usePipe command was true, it would do the same thing but under the iteration of the loop, and assigning of the malloc placements
in the dynamic array, so there are no leaks.

finally, I close all the files, run the posix_spawn command, and finish any necessary completions, so that there are no errors.

List of Additional Builtins Implemented
---------------------------------------
(Written by Your Team)
<Info> - Simple Built-In
<when you write "info" in the cush, it will tell you the directory you are in and the hostname that is running currently>

<history> - Complex built-in
<When you write "history" in the cush, it will give you the most recent 100 commands you have run in the terminal.