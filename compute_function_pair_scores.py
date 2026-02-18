import sys
import re
import numpy as np
import pickle
import hashlib
import os


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
                    r"^Defining ([a-zA-Z0-9_]+) with ([0-9]+) in file ([a-zA-Z0-9_\-\/\.]+)"
                ).findall(line)
            for fn_name, nb_argument, filename in res:
                if fn_name in context["func_def"]:
                    print(f"Error: {fn_name} defined multiple times")
                context["func_def"][fn_name] = [nb_argument, filename]

            res = re.compile(
                    r"^Calling ([a-zA-Z0-9_]+) in ([a-zA-Z0-9_\-\/\.]+):([a-zA-Z0-9_]+)"
                ).findall(line)
            for fn_name, filename, function in res:
                if fn_name not in context["call"]:
                    context["call"][fn_name] = set()
                context["call"][fn_name].add(filename)

            res = re.compile(
                    r"^funct pair: ([a-zA-Z0-9_]+) ([a-zA-Z0-9_]+) ([0-9\.]+)"
                ).findall(line)
            for fn1, fn2, score in res:
                if fn1 > fn2:
                    fn1, fn2 = fn2, fn1
                if fn1 not in context["pair_args"]:
                    context["pair_args"][fn1] = {}
                if fn2 not in context["pair_args"][fn1]:
                    context["pair_args"][fn1][fn2] = []
                context["pair_args"][fn1][fn2].append(float(score))
        
    return context


def compute_scores(context):
    res = []
    for fn1 in context["pair_args"]:
        for fn2 in context["pair_args"][fn1]:
            if (len(context["pair_args"][fn1][fn2]) < 100):
                continue

            gathered_occurrences = (len(context["call"][fn1]
                                        .intersection(context["call"][fn2])))
            gathered_occurrences_args = len(context["pair_args"][fn1][fn2])
            occurrence_corelation = ( gathered_occurrences /
                                    max(len(context["call"][fn1]), len(context["call"][fn2])))
            score = np.average(context["pair_args"][fn1][fn2]) * occurrence_corelation
            if fn2 == "dma_mapping_error" and fn1 == "dma_map_single_attrs":
                print(gathered_occurrences, gathered_occurrences_args, occurrence_corelation, score)
            if occurrence_corelation < 0.75 or gathered_occurrences_args < 100:
                continue
            res.append([score, fn1, fn2])


    res.sort()
    return res


if __name__ == "__main__":
    filename = sys.argv[1]
    file = open(filename, 'rb')
    pkl_file = hashlib.md5(file.read()).hexdigest() + ".pkl"
    file.close()


    if (os.path.isfile(pkl_file)):
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

    res = compute_scores(context)

    for i in res:
        print(i)
