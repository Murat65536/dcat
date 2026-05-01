import os
import subprocess
import pytest

DCAT_EXE = os.environ.get('DCAT_EXE')

def test_executable_exists():
    assert DCAT_EXE is not None, "DCAT_EXE environment variable not set"
    assert os.path.exists(DCAT_EXE), f"Executable not found at {DCAT_EXE}"

def test_help_output():
    """Test that running with --help outputs usage information and exits with 0."""
    result = subprocess.run([DCAT_EXE, '--help'], capture_output=True, text=True)
    assert result.returncode == 0
    assert "Usage: dcat [OPTION]... [MODEL]" in result.stdout

def test_missing_model_error():
    """Test that running without a model path fails and prints an error."""
    result = subprocess.run([DCAT_EXE], capture_output=True, text=True)
    assert result.returncode != 0
    assert "Error: No model file specified" in result.stderr
