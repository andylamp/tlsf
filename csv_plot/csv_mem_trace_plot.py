#!/usr/bin/python3
#
# This is a preliminary script for plotting the csv traces nicely
#
import numpy as np
from matplotlib import pyplot as plt
import csv
import sys
from os import remove
from os.path import splitext, basename, isfile


#  parse the csv file that contains the measurements
def parse_file(f):
    with open(f, 'r') as csv_fp:
        csv_reader = csv.reader(csv_fp)
        # skip the first two lines
        next(csv_reader)
        next(csv_reader)
        # parse rows
        malloc_len = list()
        free_len = list()
        # loop through the file and parse its lines
        for l in csv_reader:
            c_tim = int(float(l[3]))
            # append to the joined list
            # append to the individual list
            if l[0] == "malloc":
                malloc_len.append(c_tim)
            else:
                free_len.append(c_tim)

        plot_histogram_01(f, malloc_len, free_len)


def plot_histogram_01(fname, data_a, data_b):
    #strip_fname = splitext(basename(fname))[0]
    #print(strip_fname)

    bins = np.arange(0, 4000, 200)

    hist_fig, ax = plt.subplots(figsize=(9, 5))
    _, bins, patches = plt.hist([np.clip(data_a, bins[0], bins[-1]),
                                 np.clip(data_b, bins[0], bins[-1])],
                                density=True,
                                bins=bins, color=['#0087ab', '#a7cbeb'], 
                                label=['malloc', 'free'])

    xlabels = [str(b) for b in bins[1:]]
    xlabels[-1] += '+'

    num_of_labels = len(xlabels)
    plt.xlim([0, 4000])
    plt.xticks(200 * np.arange(num_of_labels) + 100)
    ax.set_xticklabels(xlabels)
    plt.xlabel("cycles spent")

    plt.yticks([])
    plt.ylabel("density")
    plt.title('')
    plt.setp(patches, linewidth=0)
    plt.legend(loc='upper right')

    hist_fig.tight_layout()
    plt.show()
    fpath = fname+".pdf"
    if isfile(fpath):
        remove(fpath)
    hist_fig.savefig(fpath, bbox_inches='tight')


if __name__ == '__main__':
    ftrace = "../traces/20180629T195418Z_mem_trace_out.csv"
    #ftrace = "../traces/20180630T223401Z_mem_trace_out.csv"
    if len(sys.argv) > 1:
        ftrace = sys.argv[1]
    # parse the file
    parse_file(ftrace)
