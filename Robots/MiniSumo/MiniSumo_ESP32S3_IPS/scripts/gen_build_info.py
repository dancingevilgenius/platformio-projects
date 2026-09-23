# PlatformIO pre-script: write include/BuildInfo.h with build time + git subject.
Import("env")  # type: ignore  # provided by PlatformIO SCons

import subprocess
from datetime import datetime
from pathlib import Path


def git_subject(project_dir: Path) -> str:
  try:
    out = subprocess.check_output(
        ["git", "log", "-1", "--pretty=%s"],
        cwd=str(project_dir),
        stderr=subprocess.DEVNULL,
    )
    msg = out.decode("utf-8", errors="replace").strip()
  except Exception:
    msg = "unknown commit"
  if not msg:
    msg = "unknown commit"
  words = msg.split()
  if len(words) > 20:
    msg = " ".join(words[:20]) + "..."
  return msg


def c_escape(s: str) -> str:
  return (
      s.replace("\\", "\\\\")
      .replace('"', '\\"')
      .replace("\r", " ")
      .replace("\n", " ")
  )


project_dir = Path(env["PROJECT_DIR"])  # type: ignore  # noqa: F821
out_path = project_dir / "include" / "BuildInfo.h"
out_path.parent.mkdir(parents=True, exist_ok=True)

build_date = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
commit_msg = git_subject(project_dir)

out_path.write_text(
    "#pragma once\n"
    f'#define BUILD_DATE "{c_escape(build_date)}"\n'
    f'#define GIT_COMMIT_MSG "{c_escape(commit_msg)}"\n',
    encoding="utf-8",
)
print(f"BuildInfo: {build_date} | {commit_msg}")
