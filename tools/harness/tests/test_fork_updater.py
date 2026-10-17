"""A numbered fork with an enabled legacy config never starts its updater."""
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, function, read, run


@pytest.mark.parametrize("fault", [False, True])
def test_desktop_policy(fault):
    source = read("port/linux/src/updater.c")
    policy = 1 if fault else constant(source, "FORK_AUTOMATIC_UPDATES")
    code = f"#define FORK_AUTOMATIC_UPDATES {policy}\n" + function(source, "updater_start")
    status, output = run(build("fork_updater", (("under_test.inc", code),)), "legacy-config")
    assert status == (CHECK_FAILED if fault else 0), output


@pytest.mark.parametrize("fault", [False, True])
def test_android_policy(fault):
    javac, java = shutil.which("javac"), shutil.which("java")
    if not javac or not java:
        pytest.skip("JDK is not installed")
    source = read("port/android/app/src/main/java/com/halo/decomp/Updater.java")
    policy = re.search(r"private static final boolean AUTOMATIC_UPDATES_ENABLED = (?:true|false);", source)[0]
    if fault:
        policy = policy.replace("false", "true")
    code = """import java.io.File;
public class UpdaterPolicyProbe {
    static int threads, checks, prompts;
    static class BuildConfig { static final int HALO_BUILD_NUMBER = 999; }
    static class Activity { void runOnUiThread(Runnable r) { r.run(); } }
    static class Thread {
        Runnable job;
        Thread(Runnable job, String name) { this.job = job; }
        void start() { threads++; job.run(); }
    }
    static File configFile(Activity a) { return new File("legacy-config.toml"); }
    static boolean autoUpdate(File f) { return true; }
    static int latestRelease() { checks++; return 1000; }
    static void ask(Activity a, int n) { prompts++; }
""" + policy + "\n" + function(source, "start") + """
    public static void main(String[] args) {
        start(new Activity());
        if (threads != 0 || checks != 0 || prompts != 0)
            throw new AssertionError("fork updater ran with an enabled legacy config");
    }
}
"""
    with tempfile.TemporaryDirectory(prefix="halo-updater-policy-") as directory:
        path = Path(directory) / "UpdaterPolicyProbe.java"
        path.write_text(code)
        compiled = subprocess.run([javac, str(path)], capture_output=True, text=True)
        assert compiled.returncode == 0, compiled.stderr
        result = subprocess.run([java, "-cp", directory, "UpdaterPolicyProbe"], capture_output=True, text=True)
        assert (result.returncode != 0) == fault, result.stderr
