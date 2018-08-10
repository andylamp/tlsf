#!/usr/bin/python3
"""csv_mem_trace_plot this script is designed to parse and plot the
trace files produced by the our memory benchmark. The trace files
are expected to have the following format:

    line 0 (plan size): plan_size
    lines 1 (headers): op_type,chunk_size,block_id,exec_time
    lines 2...n (actual traces): malloc,36757504,0,8580.000000

Usage:
    csv_mem_trace_plot.py [(-i FILE | --infile=FILE) (-f FVAL | --free_cutoff=FVAL) \
(-m MVAL | --malloc_cutoff=MVAL) (-o DIR | --out_dir=DIR) (-c | --console)]
    csv_mem_trace_plot.py (-i FILE | --in_file=FILE)
    csv_mem_trace_plot.py (-h | --help)
    csv_mem_trace_plot.py (-v | --version)

Arguments:
    -h --help                      show this message
    -i FILE --in_file=FILE         import a specific trace file
    -f FVAL --free_cutoff=FVAL     free op cutoff to count as "over budget" [default: 800].
    -m MVAL --malloc_cutoff=MVAL   malloc op cutoff to count as "over budget" [default: 2000].
    -o DIR --out_dir=DIR           location to save the generated plots [default: None].
    -v --version                   show version number.
    -c --console                   enable if executing from console to avoid errors [default: False].
"""
import numpy as np
import csv
import matplotlib
from docopt import docopt
from math import ceil
from os import remove
from os.path import basename, isfile

free_cutoff = 800
malloc_cutoff = 2000
out_dir = None
infile = "./traces/20180629T195418Z_mem_trace_out.csv"
plt = None


def parse_file(f):
    """parse the csv file that contains the traces of the executed plan

    :param f: file path of the trace file
    :return: nothing
    """
    with open(f, 'r') as csv_fp:
        csv_reader = csv.reader(csv_fp)
        # skip the first two lines
        next(csv_reader)
        next(csv_reader)
        malloc_spikes = 0
        free_spikes = 0
        # parse rows
        malloc_len = list()
        free_len = list()
        malloc_chunk_len = list()
        free_chunk_len = list()
        # loop through the file and parse its lines
        for l in csv_reader:
            c_tim = int(float(l[3]))
            c_chunk = int(l[1]) / (1024 * 1024)
            # append to the joined list
            # append to the individual list
            if l[0] == "malloc":
                malloc_len.append(c_tim)
                malloc_chunk_len.append(c_chunk)
                if c_tim > malloc_cutoff:
                    malloc_spikes += 1
            else:
                free_len.append(c_tim)
                free_chunk_len.append(c_chunk)
                if c_tim > free_cutoff:
                    free_spikes += 1

        plot_hist2d(f + "malloc", "malloc ops", malloc_len, malloc_chunk_len, 40)
        plot_hist2d(f + "free", "free ops", free_len, free_chunk_len, 40)
        plot_hist_comb(f, malloc_len, free_len, "malloc", "free", 20)
        total_ops = len(malloc_len) + len(free_len)
        print("Trace aggregate statistics")
        print("Parsed filename: {}".format(basename(f)))
        print("Cutoff threshold (in cycles): {} cycles for free and {} cycles for malloc".format(free_cutoff,
                                                                                                 malloc_cutoff))
        print("Total spikes: {} out of {} ops".format(malloc_spikes + free_spikes, total_ops))
        print("Total malloc spikes: {} out of {} ops".format(malloc_spikes, len(malloc_len)))
        print("Total free spikes: {} out of {} ops".format(free_spikes, len(free_len)))
        print("Total spike %: {} %".format((100.0 * (malloc_spikes + free_spikes)) / total_ops))
        print("Total malloc spike %: {} %".format((100.0 * malloc_spikes) / len(malloc_len)))
        print("Total free spike %: {} %".format((100.0 * free_spikes) / len(free_len)))


def plot_hist2d(fname, title_tag, data_a, data_b, bins):
    """This function plots two variables that have the same size in a 2d histogram

    :param fname: filename, which is used for the output filename
    :param title_tag: title for the graph
    :param data_a: data a
    :param data_b: data b
    :param bins: the number of bins to use
    :return:
    """
    max_value_a = int(np.mean(data_a)) * 3
    max_value_b = int(np.mean(data_b)) * 3

    bin_step_a = max_value_a / bins
    bin_step_b = max_value_b / bins

    binsa = np.arange(0, max_value_a, bin_step_a)
    binsb = np.arange(0, max_value_b, bin_step_b)

    hist2d_fig = plt.figure()
    plt.hist2d(np.clip(data_a, binsa[0], binsa[-1]),
               np.clip(data_b, binsb[0], binsb[-1]), bins=bins)
    plt.colorbar()
    plt.ylabel("Block size (MB)")
    plt.xlabel("Cycles spent")
    plt.title(str(len(data_b)) + " " + title_tag)
    plt.show()
    fpath = fname + "_chunk_vs_cycles" + ".pdf"
    if isfile(fpath):
        remove(fpath)
    hist2d_fig.savefig(fpath, bbox_inches='tight')


def plot_hist_comb(fname, data_a, data_b, data_a_tag, data_b_tag, bin_no):
    """This function plots two histograms in the same figure using the same
    bin thresholds for both

    :param fname: filename, which is used for the output filename
    :param data_a: data a
    :param data_b: data b
    :param bin_no: the number of bins to use
    :return: nothing
    """
    # find the reference for the bin clipping points
    max_value_a = int(np.mean(data_a)) * 2
    max_value_b = int(np.mean(data_b)) * 2

    # take the largest value as a reference
    if max_value_a > max_value_b:
        max_val = max_value_a
    else:
        max_val = max_value_b

    # now round up the value to the nearest number divisible by 100
    max_val = roundup(max_val, 100)
    # do the same for the bins
    bin_step = roundup(max_val / bin_no, 100)
    bins = np.arange(0, max_val, bin_step)

    hist_fig, ax = plt.subplots(figsize=(18, 5))
    _, bins, patches = plt.hist([np.clip(data_a, bins[0], bins[-1]),
                                 np.clip(data_b, bins[0], bins[-1])],
                                density=True,
                                bins=bins, color=['#0087ab', '#a7cbeb'],
                                label=[data_a_tag, data_b_tag])

    xlabels = [str(b) for b in bins[1:]]
    xlabels[-1] += '+'

    num_of_labels = len(xlabels)
    plt.xlim([0, max_val])
    plt.xticks(bin_step * np.arange(num_of_labels) + bin_step / 2)
    ax.set_xticklabels(xlabels)
    plt.xlabel("cycles spent")

    plt.yticks([])
    plt.ylabel("density")
    plt.title("cpu cycles for total ops: " + str(len(data_a) + len(data_b)))
    plt.setp(patches, linewidth=0)
    plt.legend(loc='upper right')

    hist_fig.tight_layout()
    plt.show()
    fpath = fname + ".pdf"
    if isfile(fpath):
        remove(fpath)
    hist_fig.savefig(fpath, bbox_inches='tight')


def roundup(val, base):
    """
    Rounds the value to the nearest value that is a multiple of base.
    :param val: the value to be rounded
    :param base: the base to round to
    :return: the final rounded value
    """
    return int(ceil(val / base) * base)


def parse_args():
    """
    This function parses the docopt dictionary and sets the respective globals
    to their parsed or default values.
    :return: nothing
    """
    # scope in the variables
    global free_cutoff
    global malloc_cutoff
    global out_dir
    global infile
    global plt
    # parse and generate the docopt dictionary
    arg_dict = docopt(__doc__, version="0.1.0")
    #print(arg_dict)

    # handle malloc op cutoff
    parsed_tag = tag_digit(arg_dict, "--malloc_cutoff")
    if parsed_tag == 0:
        print(" !! Error: Invalid value given for malloc cutoff, using default ({})"
              .format(malloc_cutoff))
    else:
        print(" ** Info: malloc op cutoff is: {}"
              .format(parsed_tag))
        malloc_cutoff = parsed_tag

    # handle free op cutoff
    parsed_tag = tag_digit(arg_dict, "--free_cutoff")
    if parsed_tag == 0:
        print(" !! Error: Invalid value given for free cutoff, using default ({})"
              .format(free_cutoff))
    else:
        print(" ** Info: free op cutoff is: {}"
              .format(parsed_tag))
        free_cutoff = parsed_tag

    # handle console flag
    if arg_dict["--console"] is not False:
        matplotlib.use('agg')

    # now import the plt
    from matplotlib import pyplot
    plt = pyplot

    # handle output directory
    if arg_dict["--out_dir"] is not None:
        out_dir = arg_dict["--out_dir"]
        print(" ** Info: using output directory: {}".format(out_dir))

    # handle infile
    if arg_dict["--infile"] is not None:
        infile = arg_dict["--infile"]
        print(" ** Info: using input trace file: {}".format(infile))


def tag_digit(docopt_dict, tag):
    """
    This function is responsible for parsing an argument from the docopt dictionary
    while checking if it's a number or not.
    :param docopt_dict: the docopt parsed dictionary
    :param tag: the argument tag to check the value
    :return: the value if set, zero (0) otherwise
    """
    if docopt_dict[tag] is not None and docopt_dict[tag].isdigit():
        return int(docopt_dict[tag])
    else:
        return 0


if __name__ == '__main__':
    """
    main stub, mainly parses arguments and calls the file parse method
    """
    parse_args()
    # parse the file
    parse_file(infile)
