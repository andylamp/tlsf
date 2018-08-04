import numpy as np
from matplotlib import pyplot as plt
import csv
import sys
from math import ceil
from os import remove
from os.path import splitext, basename, isfile


#  parse the csv file that contains the measurements
def parse_file(f):
    with open(f, 'r') as csv_fp:
        csv_reader = csv.reader(csv_fp)
        # skip the first two lines
        next(csv_reader)
        next(csv_reader)
        free_cutoff = 800
        malloc_cutoff = 2000
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
        plot_hist_comb(f, malloc_len, free_len, 20)
        total_ops = len(malloc_len) + len(free_len)
        print("Total spikes: {} out of {} ops".format(malloc_spikes + free_spikes, total_ops))
        print("Total malloc spikes: {} out of {} ops".format(malloc_spikes, len(malloc_len)))
        print("Total free spikes: {} out of {} ops".format(free_spikes, len(free_len)))
        print("Total spike %: {} %".format((1.0 * malloc_spikes + free_spikes) / total_ops))
        print("Total malloc spike %: {} %".format((1.0 * malloc_spikes) / len(malloc_len)))
        print("Total free spike %: {} %".format((1.0 * free_spikes) / len(free_len)))


def plot_hist2d(fname, title_tag, data_a, data_b, bins):
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
    plt.title(title_tag)
    plt.show()
    fpath = fname + "_chunk_vs_cycles" + ".pdf"
    if isfile(fpath):
        remove(fpath)
    hist2d_fig.savefig(fpath, bbox_inches='tight')


def plot_hist_comb(fname, data_a, data_b, bin_no):
    max_value_a = int(np.mean(data_a)) * 2
    max_value_b = int(np.mean(data_b)) * 2

    # take the largest

    if max_value_a > max_value_b:
        max_val = max_value_a
    else:
        max_val = max_value_b

    # now round up the value to the nearest number divisible by 100
    max_val = roundup(max_val, 100)
    # do the same for the bins
    bin_step = roundup(max_val / bin_no, 100)
    bins = np.arange(0, max_val, bin_step)

    hist_fig, ax = plt.subplots(figsize=(9, 5))
    _, bins, patches = plt.hist([np.clip(data_a, bins[0], bins[-1]),
                                 np.clip(data_b, bins[0], bins[-1])],
                                density=True,
                                bins=bins, color=['#0087ab', '#a7cbeb'],
                                label=['malloc', 'free'])

    xlabels = [str(b) for b in bins[1:]]
    xlabels[-1] += '+'

    num_of_labels = len(xlabels)
    plt.xlim([0, max_val])
    plt.xticks(bin_step * np.arange(num_of_labels) + bin_step / 2)
    ax.set_xticklabels(xlabels)
    plt.xlabel("cycles spent")

    plt.yticks([])
    plt.ylabel("density")
    plt.title('')
    plt.setp(patches, linewidth=0)
    plt.legend(loc='upper right')

    hist_fig.tight_layout()
    plt.show()
    fpath = fname + ".pdf"
    if isfile(fpath):
        remove(fpath)
    hist_fig.savefig(fpath, bbox_inches='tight')


def roundup(val, base):
    return int(ceil(val / base) * base)


if __name__ == '__main__':
    ftrace = "./traces/20180629T195418Z_mem_trace_out.csv"
    if len(sys.argv) > 1:
        ftrace = sys.argv[1]
    # parse the file
    parse_file(ftrace)