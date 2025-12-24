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
import sys
import re
import os

# Check if correct number of command-line arguments is provided
if len(sys.argv) != 5:
    print('Usage: python script.py <time1> <time2> <time3> "<description>"')
    print(
        'Example: python script.py 400 1000 2000 "Test file with high-resolution imagery"'
    )
    sys.exit(1)

# Extract times and description from command-line arguments
try:
    times = {
        "JP2Grok": float(sys.argv[1]),
        "JP2OpenJPEG": float(sys.argv[2]),
        "JP2KAK": float(sys.argv[3]),
    }
    description = sys.argv[4]
except ValueError:
    print("Error: Time values must be numbers (e.g., 400, 1000, 2000)")
    sys.exit(1)

# Sanitize description to create a valid filename
filename = re.sub(r"[^\w\s-]", "", description)  # Remove special characters
filename = re.sub(r"\s+", "_", filename.strip())  # Replace spaces with underscores
filename = filename + ".png"  # Add .png extension

# Debug: Print the filename to confirm
print(f"Saving plot as: {filename}")

# Drivers and their colors
drivers = ["JP2Grok", "JP2OpenJPEG", "JP2KAK"]
colors = ["green", "yellow", "red"]

# Prepare data for plotting
time_values = [times[driver] for driver in drivers]

# Create figure and axis
plt.figure(figsize=(8, 6))

# Plot bars with reduced spacing
bar_width = 0.33  # Width of each bar (total width for 3 bars ~1.0 to touch)
index = np.arange(len(drivers)) * bar_width  # Position bars next to each other
plt.bar(index, time_values, bar_width, color=colors, label=drivers)

# Calculate x-tick positions (center of each bar)
tick_positions = index
print(f"Bar positions (left edges): {index}")
print(f"Tick positions (bar centers): {tick_positions}")

# Add vertical lines at tick positions for debugging alignment
max_height = max(time_values) * 1.1  # Slightly above tallest bar
for pos in tick_positions:
    plt.axvline(x=pos, color="black", linestyle="--", alpha=0.5, linewidth=1)

# Customize plot
plt.xlabel("Drivers")
plt.ylabel("Time (ms)")
plt.title("Performance of GDAL Drivers on JP2 File")
plt.xticks(
    tick_positions, drivers, fontsize=8, ha="center"
)  # Center labels, smaller font
plt.legend(drivers)

# Add description text at the bottom
plt.figtext(
    0.5, 0.01, description, wrap=True, horizontalalignment="center", fontsize=10
)

# Adjust layout to prevent clipping of text
plt.tight_layout(rect=[0, 0.05, 1, 1])

# Save the plot with the sanitized description as the filename
try:
    plt.savefig(filename, bbox_inches="tight")
    if os.path.exists(filename):
        print(f"Plot successfully saved as {filename}")
    else:
        print(f"Error: Failed to save plot as {filename}")
except Exception as e:
    print(f"Error saving plot: {e}")
    sys.exit(1)

# Show plot
plt.show()
