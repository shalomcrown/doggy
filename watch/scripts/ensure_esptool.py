# Runs before compile/upload so pioarduino's penv esptool matches the pin.
Import("env")

import os
import subprocess
import sys

# ================================================================================

script = os.path.join(env.subst("$PROJECT_DIR"), "scripts", "repair_esptool.py")
subprocess.check_call([sys.executable, script])
