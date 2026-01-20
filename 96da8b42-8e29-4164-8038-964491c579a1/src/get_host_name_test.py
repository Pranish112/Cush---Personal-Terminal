#!/usr/bin/python
#
# getHostName_test: tests the getHostName built-in command, that works when you type
# info in the terminal.
#
# The info command in the terminal should print the hostname and current directory.
#

import os
import socket
from testutils import *

# sets up the tests on the console
console = setup_tests()

# makes sure that shell_prompt is printed
expect_prompt()

# Sends the info command, into the terminal
sendline("info")

# Get the expected hostname and current directory.
expected_hostname = socket.gethostname()
expected_cwd = os.getcwd()

# Checks that the hostname is printed.
# or else tell the user there is an error and the host name is not printed correctly
expect(expected_hostname, "Hostname is not printed correctly by the info command")

# Checks that the current directory is printed.
# or tells the terminal that the current directory is not printed correctly
expect(expected_cwd, "Current directory is not printed correctly by the info command")

# Ensure the prompt is printed again.
# if the cush>, doesn't show again, it will tell user that the shell didn't print
expect_prompt("Shell did not print expected prompt after info command")

test_success()