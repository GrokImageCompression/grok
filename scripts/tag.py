import sys
import subprocess

if len(sys.argv) != 2:
    print("Usage: python script.py <TAG>")
    sys.exit(1)

TAG = sys.argv[1]

commands = [
    ["git", "push", "origin", f":{TAG}"],
    ["git", "tag", "-d", TAG],
    ["git", "tag", "-a", TAG, "-m", TAG],
    ["git", "push", "origin", TAG]
]

for cmd in commands:
    try:
        subprocess.run(cmd, check=True, capture_output=True, text=True)
    except subprocess.CalledProcessError as e:
        print(f"Error executing {' '.join(cmd)}: {e.stderr}")
        sys.exit(1)

print(f"Tag '{TAG}' successfully deleted, recreated, and pushed to origin.")
