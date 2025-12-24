# Copyright (C) 2016-2026 Grok Image Compression Inc.
#
# This source code is free software: you can redistribute it and/or modify
# it under the terms of the GNU Affero General Public License, version 3,
# as published by the Free Software Foundation.
#
# This source code is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Affero General Public License for more details.
#
# You should have received a copy of the GNU Affero General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
import matplotlib.pyplot as plt
import numpy as np

# Data for the four graphs
drivers = ["JP2Grok", "JP2OpenJPEG", "JP2KAK"]
# Full Image, Local File
full_local_no_tlm = [28.6, 48.8, 29.2]
full_local_tlm = [26.8, 48.4, 27.2]
# Partial Read, Local File
partial_local_no_tlm = [2.6, 4.0, 3.5]
partial_local_tlm = [2.4, 3.4, 3.5]
# Full Image, MinIO
full_minio_no_tlm = [46, 645, 820]
full_minio_tlm = [47.2, 140.1, 821]
# Partial Read, MinIO
partial_minio_no_tlm = [38.5, 130, 105]
partial_minio_tlm = [9.6, 24.1, 44]


# Plotting function
def plot_bar_graph(data_no_tlm, data_tlm, title, filename, ylabel="Time (s)"):
    x = np.arange(len(drivers))
    width = 0.35

    fig, ax = plt.subplots(figsize=(8, 6))
    ax.bar(x - width / 2, data_no_tlm, width, label="No TLM", color="skyblue")
    ax.bar(x + width / 2, data_tlm, width, label="With TLM", color="lightcoral")

    ax.set_xlabel("Driver")
    ax.set_ylabel(ylabel)
    ax.set_title(title)
    ax.set_xticks(x)
    ax.set_xticklabels(drivers)
    ax.legend()

    plt.tight_layout()
    plt.savefig(filename)
    plt.close()


# Generate the four graphs
plot_bar_graph(
    full_local_no_tlm,
    full_local_tlm,
    "Full Image Conversion, Local File",
    "full_image_local.png",
)
plot_bar_graph(
    partial_local_no_tlm,
    partial_local_tlm,
    "Partial Read (6000x6000), Local File",
    "partial_read_local.png",
)
plot_bar_graph(
    full_minio_no_tlm,
    full_minio_tlm,
    "Full Image Conversion, MinIO (50ms latency)",
    "full_image_minio.png",
)
plot_bar_graph(
    partial_minio_no_tlm,
    partial_minio_tlm,
    "Partial Read (6000x6000), MinIO (50ms latency)",
    "partial_read_minio.png",
)
