#!/bin/python3

import sys
import re
import numpy as np
import pickle
import hashlib
import os
from pathlib import Path
import argparse
import matplotlib.pyplot as plt


def parse(filename):
    context = {
        # [nb argument, filename]
        "func_def" : {},
        # call[fn] is the list of files in which fn is called
        "call" : {},
        # pair_args[fn1][fn2] is the list of avg of common arguments 
        "pair_args" : {},
        # called_together[fn1][fn2] is the correlation between fn1 and fn2
        "called_together" : {},
    }


    with open(filename) as common_arg_file:
        for line in common_arg_file:
            res = re.compile(
                    r"^Defining ([a-zA-Z0-9_]+) with ([0-9]+) arguments in file ([a-zA-Z0-9_\-\/\.]+)"
                ).findall(line)
            for fn_name, nb_argument, filename in res:
                if fn_name in context["func_def"]:
                    continue
                context["func_def"][fn_name] = [int(nb_argument), filename]

            res = re.compile(
                    r"^Calling ([a-zA-Z0-9_]+) in ([a-zA-Z0-9_\-\/\.]+):([a-zA-Z0-9_]+)"
                ).findall(line)
            for fn_name, filename, function in res:
                if fn_name not in context["call"]:
                    context["call"][fn_name] = set()
                context["call"][fn_name].add(filename)

            res = re.compile(
                    r"^Same argument: ([a-zA-Z0-9_]+).([0-9]+) ([a-zA-Z0-9_]+).([0-9]+) ([0-9\.]+)"
                ).findall(line)
            for fn1, arg1, fn2, arg2, score in res:
                arg1, arg2 = int(arg1), int(arg2)
                if fn1 > fn2:
                    fn1, fn2 = fn2, fn1
                    arg1, arg2 = arg2, arg1
                if (fn1,fn2) not in context["pair_args"]:
                    context["pair_args"][fn1, fn2] = {}
                if (arg1, arg2) not in context["pair_args"][fn1, fn2]:
                    context["pair_args"][fn1, fn2][arg1, arg2] = []
                context["pair_args"][fn1, fn2][arg1, arg2].append(float(score))

    return context

def create_line(fn, args):
    res = ""
    if args[0] != "_":
        res += args[0]
        res += " = "
    
    res += fn
    res += "("

    for i in range(1, len(args)):
        res += args[i]
        res += ", "
    
    res = res[:-2]
    res += ")"
    return res

def generate_one_file(fn1, fn2, args, context):
    if fn1 in context["func_def"]:
        args1 = ["_"] * (context["func_def"][fn1][0] + 1)
    else:
        args1 = ["_"] * 10
    if fn2 in context["func_def"]:
        args2 = ["_"] * (context["func_def"][fn2][0] + 1)
    else:
        args2 = ["_"] * 10

    arg_decl = ""
    i = 0

    arg_decl += "var "
    for arg1, arg2 in args:
        this_arg = "var" + str(i)
        i += 1
        arg_decl += this_arg  + ", "


        args1[arg1] = this_arg
        args2[arg2] = this_arg

    arg_decl = arg_decl[:-2]

    arg_decl += "\n\n"
    arg_decl += create_line(fn1, args1)
    arg_decl += "\n"
    arg_decl += create_line(fn2, args2)
    arg_decl += "\n"

    return arg_decl


def generate_file(folder, functions, context):
    if os.path.exists(folder):
        os.system("rm -rf " + str(folder))
    os.mkdir(folder)
    for fn1, fn2 in sorted(functions.keys()):
        if len(functions[fn1, fn2]) == 1:
            continue
        base_filename = folder / Path(fn1)
        filename = base_filename
        i = 0
        while os.path.isfile(filename):
            filename = str(base_filename) + "_" + str(i)
            i += 1
        with open(filename, "w") as file:
            contents = generate_one_file(fn1, fn2, functions[fn1, fn2], context)
            file.write(contents)
    return


def compute_scores(min_occurrences, min_scores, context):
    res = []
    functions = {}
    for fn1, fn2 in context["pair_args"]:

        if (fn1 not in context["call"] or fn2 not in context["call"]):
            continue

        gathered_occurrences = (len(context["call"][fn1]
                                    .intersection(context["call"][fn2])))
        occurrence_corelation = ( gathered_occurrences /
                                max(len(context["call"][fn1]), len(context["call"][fn2])))
        if gathered_occurrences < min_occurrences and occurrence_corelation < min_scores:
            continue

        for arg1, arg2 in context["pair_args"][fn1, fn2]:
            gathered_occurrences_args = len(context["pair_args"][fn1, fn2][arg1, arg2])

            score = np.average(context["pair_args"][fn1, fn2][arg1, arg2]) * occurrence_corelation
            if gathered_occurrences_args < min_occurrences:
                continue

            if score < min_scores:
                continue

            res.append([score, fn1, arg1, fn2, arg2, gathered_occurrences])
            if (fn1, fn2) not in functions:
                functions[fn1, fn2] = []

            functions[fn1, fn2].append([arg1, arg2])

    res.sort()
    return res, functions

def graph_for_nb_calls(point, correct_reports, false_positives, accuracy):
    width = (point[-1] - point[0]) / len(point)

    fig, ax = plt.subplots()
    correct_bar = ax.bar(point, correct_reports, width=width, color="blue",
                         label="correct interfaces")
    ax.bar_label(correct_bar, labels=correct_reports, label_type="center",
                 color="white")
    bars = ax.bar(point, false_positives, width=width, color="orange",
                  bottom=correct_reports, label="wrong interfaces")
    ax.bar_label(bars, labels=false_positives, label_type="center")
    ax.bar_label(bars, labels=accuracy, padding=3, fontsize=8)
    ax.set_ylim(0, ax.get_ylim()[1] * 1.15)

    ax.legend(loc="upper right")
    ax.margins(tight=True)

    ax.set_ylabel("Number of reports")
    ax.set_xlabel("Threshold on the number of files")

    fig.set_size_inches(4.5, 2.5)
    fig.tight_layout()
    return fig, ax

def generate_graph(context, graph_file):
    score_threshold = np.linspace(0.4, 1.0, 12)
    right_interface = []
    wrong_interface = []
    accuracy = []
    known_wrong_interfaces = {
        "amdgpu_device_rreg",
        "amdgpu_device_wreg", 
        "clear_bit", 
        "copy_from_user", 
        "__create_pipe", 
        "debugfs_create_dir", 
        "__devm_of_phy_provider_register", 
        "devm_regulator_bulk_get", 
        "devm_request_irq", 
        "hrtimer_setup", 
        "init_timer_key", 
        "__iwl_dbg", 
        "make_kgid", 
        "netif_set_real_num_rx_queues", 
        "pci_read_config_dword", 
        "snd_pcm_new", 
        "v4l2_ctrl_handler_init_class",
    }

    interfaces_with_reports = {
        "add_wait_queue", 
        "amdgpu_device_rreg", 
        "amdgpu_device_wreg", 
        "amdgpu_sriov_rreg", 
        "clear_bit", 
        "clk_bulk_disable_unprepare", 
        "copy_from_user", 
        "__create_pipe", 
        "debugfs_create_dir", 
        "device_create_file", 
        "__devm_iio_device_register", 
        "devm_regulator_bulk_get", 
        "devm_regulator_bulk_get_0", 
        "devm_request_irq", 
        "dma_alloc_coherent", 
        "dma_mapping_error", 
        "dma_map_sg_attrs", 
        "dma_map_single_attrs", 
        "dma_sync_single_for_cpu", 
        "free_irq", 
        "i2c_smbus_read_byte_data", 
        "init_timer_key", 
        "input_report_abs", 
        "nla_nest_cancel", 
        "nla_nest_end", 
        "pci_iomap", 
        "pci_read_config_byte", 
        "pci_read_config_dword", 
        "pci_read_config_word", 
        "qcom_cc_map", 
        "rb_erase", 
        "regmap_bulk_read", 
        "regmap_clear_bits", 
        "regmap_read", 
        "regmap_read_0", 
        "regmap_update_bits", 
        "regulator_bulk_disable", 
        "__release_region", 
        "skb_pull", 
        "sysfs_create_group", 
        "unsorted", 
        "usb_alloc_urb", 
        "vb2_plane_size", 
        "__wake_up"
    }

    for x in score_threshold:
        _, funcs = compute_scores(args.nb_occurrences, x, context)

        print(funcs.keys())
        right_funcs = interfaces_with_reports.intersection(map(lambda x :x[0], funcs.keys()))
        wrong_funcs = known_wrong_interfaces.intersection(map(lambda x :x[0], funcs.keys()))

        wrong_interface.append(len(wrong_funcs))
        right_interface.append(len(right_funcs))
        acc = round(right_interface[-1] / (wrong_interface[-1] + right_interface[-1]) * 100)
        accuracy.append(str(acc) + "%")

        print(wrong_interface, right_interface, accuracy)

    graph_for_nb_calls(score_threshold, right_interface,
                       wrong_interface, accuracy)[0].savefig(graph_file)

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--pkl")
    parser.add_argument("--log")
    parser.add_argument("--output")
    parser.add_argument("--nb-occurrences", default=100, type=int)
    parser.add_argument("--min-score", default=.75, type=float)
    parser.add_argument("--graph-output")
    parser.add_argument("--tmpdir", default="/tmp/generated_spec")

    args = parser.parse_args()

    if (args.pkl):
        pkl_file = args.pkl
    else:
        if (not args.log):
            print("You need to pass a file with --log XXX", file=sys.stderr)
            exit(1)
        filename = args.log
        file = open(filename, 'rb')
        pkl_file = hashlib.md5(file.read()).hexdigest() + ".pkl"
        file.close()


    if os.path.isfile(pkl_file):
        print("Found Pickle file, using that.")
        file = open(pkl_file, 'rb')
        context = pickle.load(file)
        file.close()
    else:
        print("Parsing file")
        context = parse(filename)
        file = open(pkl_file, 'wb')
        print("Writing Pickle for later use")
        pickle.dump(context, file)
        file.close()

    print("Parsing done!")

    
    if (args.output):
        res, functions = compute_scores(args.nb_occurrences, args.min_score, context)
        generate_file(args.output, functions, context)

        for i in res:
            print(i)

    if (args.graph_output):
        generate_graph(context, args.graph_output)

