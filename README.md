Custom Unix Shell - Cush (Job Control & Process Management)

Language: C (POSIX)
Domain: Operating Systems, Systems Programming

Overview
This project is a Unix-like command shell implemented in C, designed to explore low-level operating system concepts such as process management, job control, signal handling, and terminal control.
The shell supports running programs in the foreground and background, managing multiple concurrent jobs, and safely coordinating access to the terminal — behavior consistent with modern Unix shells.

Key Features:
  Process Execution
    - Launches external programs using POSIX APIs
    - Supports both foreground and background execution
    - Tracks running, stopped, and completed jobs
    
  Job Control
    - Job abstraction using process groups
    - Correct signal delivery to foreground jobs
    - Background job monitoring and status updates
    
  Built-in Commands
    - Job listing and control (jobs, fg, bg)
    - Signal management (kill, stop)
    - Shell termination (exit)
    
  Pipelines & Redirection
    - Command pipelines (|, |&)
    - Input/output redirection
    - Append and combined stdout/stderr redirection
    
  Terminal Management
    - Foreground process group ownership
    - Terminal state preservation and restoration
    - Graceful handling of interactive programs
    
  Technical Highlights
    - Signal-safe job tracking with SIGCHLD
    - Process group management for correct job semantics
    - Careful synchronization between signal handlers and shell state
    - Clean separation between parsing, execution, and job control logic

  This Project Demonstrates
    - Strong understanding of Unix process model
    - Experience with concurrent systems programming
    - Safe and robust handling of asynchronous events
    - Ability to build real, low-level tooling from scratch

  Repository Notes
    - This code is shared for educational and portfolio purposes
    - Not intended to be used as a reference solution
    - Core logic may be simplified or abstracted for public viewing
