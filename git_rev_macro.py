import os
import re
import subprocess


firmwareVersion = os.environ.get("HDS_FIRMWARE_VERSION")
if firmwareVersion is not None and re.fullmatch(
    r"[0-9]+\.[0-9]+\.[0-9]+(?:-[a-z0-9]+(?:-[a-z0-9]+)*)?", firmwareVersion
) is None:
    raise SystemExit("HDS_FIRMWARE_VERSION must match X.Y.Z with an optional safe suffix")

revision = (
    subprocess.check_output(["git", "rev-parse", "HEAD"])
    .strip()
    .decode("utf-8")
    
)
print("'-DGIT_REV=\"%s\"'" % revision[:6])
if firmwareVersion is not None:
    print("'-DHDS_FIRMWARE_VERSION=\"%s\"'" % firmwareVersion)
