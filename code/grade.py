#!/usr/bin/env python3

import os
import subprocess
import signal
import math
import re

# Test multiplier
timeout_multiplier = 1 # increase to extend all timeouts
iters_multiplier = 1 # increase to test the code with more iterations

# Timeouts (seconds)
timeout_channel = 3 * timeout_multiplier
timeout_sanitize = 5 * timeout_multiplier
timeout_valgrind = 10 * timeout_multiplier
timeout_cpu_utilization = 40 * timeout_multiplier
timeout_response_time = 40 * timeout_multiplier
timeout_basic_global_declaration = 30 * timeout_multiplier
timeout_too_many_wakeups = 20 * timeout_multiplier
timeout_stress_send_recv = 10 * timeout_multiplier
timeout_non_blocking_receive = 5 * timeout_multiplier
timeout_select_mixed_buffered_unbuffered = 5 * timeout_multiplier
timeout_make = 30 * timeout_multiplier

# Number of iterations to run tests
iters_channel = math.ceil(5000 * iters_multiplier)
iters_sanitize = math.ceil(1000 * iters_multiplier)
iters_valgrind = math.ceil(500 * iters_multiplier)
iters_slow = math.ceil(30 * iters_multiplier)
iters_one = math.ceil(1 * iters_multiplier)

# Test cases
test_cases = {}

def add_test_case_channel(test_name, iters=0, timeout=0):
    test_cases["channel_{}".format(test_name)] = {"args": ["./channel", test_name, str(iters_channel if iters == 0 else iters)], "timeout": timeout_channel if timeout == 0 else timeout}

def add_test_case_sanitize(test_name, iters=0, timeout=0):
    test_cases["sanitize_{}".format(test_name)] = {"args": ["./channel_sanitize", test_name, str(iters_sanitize if iters == 0 else iters)], "timeout": timeout_sanitize if timeout == 0 else timeout}

def add_test_case_valgrind(test_name, iters=0, timeout=0):
    test_cases["valgrind_{}".format(test_name)] = {"args": ["valgrind", "-v", "--leak-check=full", "--errors-for-leak-kinds=all", "--error-exitcode=2", "./channel", test_name, str(iters_valgrind if iters == 0 else iters)], "timeout": timeout_valgrind if timeout == 0 else timeout}

def add_test_cases(test_name, iters=0, timeout=0):
    add_test_case_channel(test_name, iters, timeout)
    add_test_case_sanitize(test_name, iters, timeout)
    add_test_case_valgrind(test_name, iters, timeout)

add_test_cases("test_initialization")
add_test_cases("test_free")
add_test_cases("test_send_correctness", iters_slow)
add_test_cases("test_receive_correctness", iters_slow)
add_test_cases("test_non_blocking_send", iters_one)
add_test_cases("test_non_blocking_receive", iters_one, timeout_non_blocking_receive)
add_test_cases("test_multiple_channels")
add_test_case_channel("test_overall_send_receive", iters_one)
add_test_case_sanitize("test_overall_send_receive", iters_one)
add_test_case_valgrind("test_overall_send_receive", iters_one, timeout_valgrind * 5)
add_test_cases("test_stress_send_recv_buffered", iters_one, timeout_stress_send_recv)
add_test_cases("test_response_time", iters_one, timeout_response_time)
add_test_cases("test_cpu_utilization_send", iters_one, timeout_cpu_utilization)
add_test_cases("test_cpu_utilization_receive", iters_one, timeout_cpu_utilization)
add_test_case_channel("test_channel_close_with_send", iters_slow)
add_test_case_sanitize("test_channel_close_with_send", iters_slow)
add_test_case_valgrind("test_channel_close_with_send", iters_slow, timeout_valgrind * 2)
add_test_case_channel("test_channel_close_with_receive", iters_slow)
add_test_case_sanitize("test_channel_close_with_receive", iters_slow)
add_test_case_valgrind("test_channel_close_with_receive", iters_slow, timeout_valgrind * 2)
add_test_cases("test_select", iters_slow)
add_test_cases("test_select_close", iters_slow)
add_test_cases("test_select_and_non_blocking_send_buffered", iters_slow)
add_test_cases("test_select_and_non_blocking_receive_buffered", iters_slow)
add_test_cases("test_select_with_select_buffered", iters_slow)
add_test_cases("test_select_with_same_channel_buffered")
add_test_cases("test_select_with_send_receive_on_same_channel_buffered")
add_test_cases("test_select_with_duplicate_channel_buffered", iters_slow)
add_test_case_channel("test_stress_buffered", iters_one, timeout_channel * 5)
add_test_case_sanitize("test_stress_buffered", iters_one, timeout_sanitize * 5)
add_test_case_valgrind("test_stress_buffered", iters_one, timeout_valgrind * 5)
add_test_cases("test_select_response_time", iters_one, timeout_response_time)
add_test_cases("test_cpu_utilization_select", iters_one, timeout_cpu_utilization)
add_test_cases("test_for_basic_global_declaration", iters_one, timeout_basic_global_declaration)
add_test_cases("test_for_too_many_wakeups", iters_one, timeout_too_many_wakeups)

# Score distribution
point_breakdown = [
    # Basic (125 pts)
    (3, ["channel_test_initialization"]),
    (3, ["sanitize_test_initialization"]),
    (3, ["valgrind_test_initialization"]),
    (3, ["channel_test_free"]),
    (3, ["sanitize_test_free"]),
    (3, ["valgrind_test_free"]),
    (3, ["channel_test_send_correctness"]),
    (3, ["sanitize_test_send_correctness"]),
    (3, ["valgrind_test_send_correctness"]),
    (3, ["channel_test_receive_correctness"]),
    (3, ["sanitize_test_receive_correctness"]),
    (3, ["valgrind_test_receive_correctness"]),
    (3, ["channel_test_non_blocking_send"]),
    (3, ["sanitize_test_non_blocking_send"]),
    (3, ["valgrind_test_non_blocking_send"]),
    (3, ["channel_test_non_blocking_receive"]),
    (3, ["sanitize_test_non_blocking_receive"]),
    (3, ["valgrind_test_non_blocking_receive"]),
    (4, ["channel_test_multiple_channels"]),
    (4, ["sanitize_test_multiple_channels"]),
    (4, ["valgrind_test_multiple_channels"]),
    (4, ["channel_test_overall_send_receive"]),
    (4, ["sanitize_test_overall_send_receive"]),
    (4, ["valgrind_test_overall_send_receive"]),
    (10, ["channel_test_stress_send_recv_buffered"]),
    (10, ["sanitize_test_stress_send_recv_buffered"]),
    (10, ["valgrind_test_stress_send_recv_buffered"]),
    (2, ["channel_test_response_time"]),
    (2, ["sanitize_test_response_time"]),
    (2, ["valgrind_test_response_time"]),
    (2, ["channel_test_cpu_utilization_send"]),
    (2, ["sanitize_test_cpu_utilization_send"]),
    (2, ["valgrind_test_cpu_utilization_send"]),
    (1, ["channel_test_cpu_utilization_receive"]),
    (2, ["sanitize_test_cpu_utilization_receive"]),
    (2, ["valgrind_test_cpu_utilization_receive"]),

    # Close (24 pts)
    (4, ["channel_test_channel_close_with_send"]),
    (4, ["channel_test_channel_close_with_receive"]),
    (4, ["sanitize_test_channel_close_with_send"]),
    (4, ["sanitize_test_channel_close_with_receive"]),
    (4, ["valgrind_test_channel_close_with_send"]),
    (4, ["valgrind_test_channel_close_with_receive"]),

    # Select (51 pts)
    (1, ["channel_test_select"]),
    (1, ["sanitize_test_select"]),
    (1, ["valgrind_test_select"]),
    (1, ["channel_test_select_close"]),
    (1, ["sanitize_test_select_close"]),
    (1, ["valgrind_test_select_close"]),
    (1, ["channel_test_select_and_non_blocking_send_buffered"]),
    (1, ["sanitize_test_select_and_non_blocking_send_buffered"]),
    (1, ["valgrind_test_select_and_non_blocking_send_buffered"]),
    (1, ["channel_test_select_and_non_blocking_receive_buffered"]),
    (1, ["sanitize_test_select_and_non_blocking_receive_buffered"]),
    (1, ["valgrind_test_select_and_non_blocking_receive_buffered"]),
    (1, ["channel_test_select_with_select_buffered"]),
    (1, ["sanitize_test_select_with_select_buffered"]),
    (1, ["valgrind_test_select_with_select_buffered"]),
    (1, ["channel_test_select_with_same_channel_buffered"]),
    (1, ["sanitize_test_select_with_same_channel_buffered"]),
    (1, ["valgrind_test_select_with_same_channel_buffered"]),
    (1, ["channel_test_select_with_send_receive_on_same_channel_buffered"]),
    (1, ["sanitize_test_select_with_send_receive_on_same_channel_buffered"]),
    (1, ["valgrind_test_select_with_send_receive_on_same_channel_buffered"]),
    (1, ["channel_test_select_with_duplicate_channel_buffered"]),
    (1, ["sanitize_test_select_with_duplicate_channel_buffered"]),
    (1, ["valgrind_test_select_with_duplicate_channel_buffered"]),
    (5, ["channel_test_stress_buffered"]),
    (5, ["sanitize_test_stress_buffered"]),
    (5, ["valgrind_test_stress_buffered"]),
    (1, ["channel_test_select_response_time"]),
    (1, ["sanitize_test_select_response_time"]),
    (1, ["valgrind_test_select_response_time"]),
    (1, ["channel_test_cpu_utilization_select"]),
    (1, ["sanitize_test_cpu_utilization_select"]),
    (1, ["valgrind_test_cpu_utilization_select"]),
    (1, ["channel_test_for_basic_global_declaration"]),
    (1, ["sanitize_test_for_basic_global_declaration"]),
    (1, ["valgrind_test_for_basic_global_declaration"]),
    (1, ["channel_test_for_too_many_wakeups"]),
    (1, ["sanitize_test_for_too_many_wakeups"]),
    (1, ["valgrind_test_for_too_many_wakeups"]),
]

def print_success(test):
    print("****SUCCESS: {}****".format(test))

def print_failed(test):
    print("****FAILED: {}****".format(test))

def make_assignment():
    args = ["make", "clean", "all"]
    try:
        subprocess.check_output(args, stderr=subprocess.STDOUT, timeout=timeout_make)
        return True
    except subprocess.CalledProcessError as e:
        print_failed("make")
        print(e.output.decode())
    except subprocess.TimeoutExpired as e:
        print_failed("make")
        print("Failed to compile within {} seconds".format(e.timeout))
    except KeyboardInterrupt:
        print_failed("make")
        print("User interrupted compilation")
    except:
        print_failed("make")
        print("Unknown error occurred")
    return False

def check_global_variables():
    global_variables = []
    for name in ["channel", "linked_list"]:
        error = ""
        args = ["nm", "-f", "posix", "{}.o".format(name)]
        try:
            output = subprocess.check_output(args, stderr=subprocess.STDOUT).decode()
            for line in output.splitlines():
                if re.search(" [BbCcDdGgSsVvWw] ", line):
                    global_variables.append("{}.c: {}".format(name, line.split(" ", 1)[0]))
        except subprocess.CalledProcessError as e:
            error = e.output.decode()
        except KeyboardInterrupt:
            error = "User interrupted global variable test"
        except:
            error = "Unknown error occurred"
        if error != "":
            print_failed("check_global_variables")
            print(error)
            return False
    if len(global_variables) > 0:
        print_failed("check_global_variables")
        print("You are not allowed to use global variables in this assignment:")
        for global_variable in global_variables:
            print(global_variable)
        return False
    return True

def grade():
    result = {}

    # Run make on the assignment
    if make_assignment() and check_global_variables():
        # Run test cases
        for test, config in test_cases.items():
            try:
                output = subprocess.check_output(config["args"], stderr=subprocess.STDOUT, timeout=config["timeout"]).decode()
                if "ALL TESTS PASSED" in output:
                    result[test] = True
                    print_success(test)
                else:
                    result[test] = False
                    print_failed(test)
                    error_log = "error_{}.log".format(test)
                    with open(error_log, "w") as fp:
                        fp.write(output)
                    print("See {} for error details".format(error_log))
            except subprocess.CalledProcessError as e:
                result[test] = False
                print_failed(test)
                if e.returncode < 0:
                    if -e.returncode == signal.SIGSEGV:
                        print("Segmentation fault (core dumped)")
                    else:
                        print("Died with signal {}".format(-e.returncode))
                error_log = "error_{}.log".format(test)
                with open(error_log, "w") as fp:
                    fp.write(e.output.decode())
                print("See {} for error details".format(error_log))
            except subprocess.TimeoutExpired as e:
                result[test] = False
                print_failed(test)
                print("Failed to complete within {} seconds".format(e.timeout))
            except KeyboardInterrupt:
                result[test] = False
                print_failed(test)
                print("User interrupted test")
            except:
                result[test] = False
                print_failed(test)
                print("Unknown error occurred")

    # Calculate score
    score = 0
    total_possible = 0
    for points, tests in point_breakdown:
        if all(map(lambda test : test in result and result[test], tests)):
            score += points
        total_possible += points
    return (score, total_possible)

if __name__ == "__main__":
    score, total_possible = grade()
    print("Final score: {}".format(score))
