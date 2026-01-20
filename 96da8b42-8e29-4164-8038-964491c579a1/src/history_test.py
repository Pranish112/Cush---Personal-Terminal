#!/usr/bin/python
#
# history_test: tests the history built-in command of cush.
#
import sys, atexit, pexpect, time
from testutils import *

#initializes the tests
console = setup_tests()

# Ensures that the shell prompt appears.
expect_prompt("Prompt did not appear.")

# Send a few commands to build history.
sendline("echo hello")
# should print "hello" or the expected output was not obtained
expect("hello", "echo did not print expected output")
# cush> shoudl be shown after the previous command is done
expect_prompt("Prompt not shown after echo command.")

# send info into the cush
sendline("info")
# info should print the hostname and the current directory.
expect("Hostname:", "info did not print hostname")
expect("Current Directory:", "info did not print current directory")
expect_prompt("Prompt not shown after info command.")

# Now issue the history command.
sendline("history")
# Check that the previously entered commands are printed.
# (The exact format is: "1: <command>" so we look for the command text.)
expect("echo hello", "History does not list the 'echo hello' command.")
expect("info", "History does not list the 'info' command.")
expect_prompt("Prompt not shown after history command.")

test_success()
