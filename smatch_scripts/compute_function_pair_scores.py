import sys
import re
import numpy as np
import pickle
import hashlib
import os
from pathlib import Path
import argparse


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


def compute_scores(min_occurrences, context):
    res = []
    functions = {}
    for fn1, fn2 in context["pair_args"]:

        if (fn1 not in context["call"] or fn2 not in context["call"]):
            continue

        gathered_occurrences = (len(context["call"][fn1]
                                    .intersection(context["call"][fn2])))
        occurrence_corelation = ( gathered_occurrences /
                                max(len(context["call"][fn1]), len(context["call"][fn2])))
        if gathered_occurrences < min_occurrences or occurrence_corelation < 0.75:
            continue

        for arg1, arg2 in context["pair_args"][fn1, fn2]:
            gathered_occurrences_args = len(context["pair_args"][fn1, fn2][arg1, arg2])

            score = np.average(context["pair_args"][fn1, fn2][arg1, arg2]) * occurrence_corelation
            if gathered_occurrences_args < min_occurrences:
                continue

            if score < 0.75:
                continue

            res.append([score, fn1, arg1, fn2, arg2, gathered_occurrences])
            if (fn1, fn2) not in functions:
                functions[fn1, fn2] = []

            functions[fn1, fn2].append([arg1, arg2])

    res.sort()
    return res, functions


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--pkl")
    parser.add_argument("--log")
    parser.add_argument("--output")
    parser.add_argument("--nb-occurrences", default=100)

    args = parser.parse_args()

    if (args.pkl):
        pkl_file = args.pkl
    else:
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

    res, functions = compute_scores(args.nb_occurrences, context)
    if (args.output):
        generate_file(args.output, functions, context)

    for i in res:
        print(i)
