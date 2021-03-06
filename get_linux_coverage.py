import os
import subprocess
import shutil
import webbrowser
from itertools import chain

demos = []
demo_folder = "demos"
# Usual Linux excludes
excludes = ["stackcheck", "selfyield", "alloc"]

# Find all demos
for f in os.listdir(demo_folder):
  if os.path.isfile(os.path.join(demo_folder, f)) and \
      f.endswith(".c"):
    demos.append(os.path.splitext(f)[0])
demos = [d for d in demos if d not in excludes]

# Folder to store all coverage data
if os.path.isdir("cov"):
  shutil.rmtree("cov")
os.mkdir("cov")

# Sub folders for each demo's coverage
map(os.mkdir, ["cov/{}_cov".format(d) for d in demos])

# Configure for Linux
subprocess.check_call(["cmake", ".",
    "-DBUILD_PLATFORM=linux",
    "-DOPT_LEVEL=0",
    "-DCOVERAGE=ON"])

cov_dirs = []
for demo in demos:
  subprocess.check_call(["make", "clean"])
  subprocess.check_call(["make", "run_{}".format(demo)])

  cov_folder = "cov/{}_cov/".format(demo)
  cov_dirs.append(cov_folder)

  # Copy all coverage files to the demo's folder
  for root, dirs, files in os.walk("CMakeFiles"):
    for f in files:
      if f.endswith(".gcda") or f.endswith(".gcno"):
        path = os.path.join(root, f)
        shutil.move(path, os.path.join(cov_folder, f))

# Generate report
largs = ["lcov", "-c"]
largs.extend(chain(*[("-d", d) for d in cov_dirs]))
largs.extend(["--output-file", "overall_coverage.info"])
subprocess.check_call(largs)

# Generate HTML report
subprocess.check_call(["genhtml",
    "overall_coverage.info",
    "--output-directory",
    "out"])
webbrowser.open("out/index.html")
