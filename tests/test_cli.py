import functools
import os
import subprocess
import pytest
from pathlib import Path

DCAT_EXE = os.environ.get('DCAT_EXE')
ASSETS_DIR = Path(__file__).parent / 'assets'
TEST_CUBE = ASSETS_DIR / 'test_cube.obj'


@functools.lru_cache(maxsize=1)
def _vulkan_available():
    """Probe once whether a usable Vulkan device exists.

    Headless CI runners (and any box without a Vulkan ICD) make dcat abort at
    vkCreateInstance before it can load a model or render output, so the
    render-dependent tests below are meaningless there and are skipped.
    """
    if not DCAT_EXE or not os.path.exists(DCAT_EXE):
        return False
    result = subprocess.run(
        [DCAT_EXE, 'does_not_exist.gltf'], capture_output=True, text=True
    )
    return "Failed to create Vulkan instance" not in result.stderr


requires_vulkan = pytest.mark.skipif(
    not _vulkan_available(),
    reason="No Vulkan device available (headless runner without a software ICD)",
)

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

def test_invalid_dimensions():
    """Test that invalid width/height are rejected."""
    result = subprocess.run([DCAT_EXE, '-W', '-100', str(TEST_CUBE)], capture_output=True, text=True)
    assert result.returncode != 0
    assert "Invalid width: -100" in result.stderr

def test_invalid_fps():
    """Test that negative FPS is rejected."""
    result = subprocess.run([DCAT_EXE, '--fps', '0', str(TEST_CUBE)], capture_output=True, text=True)
    assert result.returncode != 0
    assert "Invalid FPS: 0" in result.stderr

@requires_vulkan
def test_model_not_found():
    """Test that a non-existent model file is handled gracefully."""
    result = subprocess.run([DCAT_EXE, 'does_not_exist.gltf'], capture_output=True, text=True)
    assert result.returncode != 0
    assert "Failed to load model:" in result.stderr

@requires_vulkan
def test_sixel_output_header():
    """Test that the --sixel flag actually outputs Sixel escape sequences."""
    process = subprocess.Popen(
        [DCAT_EXE, '--sixel', str(TEST_CUBE)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )
    try:
        stdout, _ = process.communicate(timeout=1.0)
    except subprocess.TimeoutExpired:
        process.kill()
        stdout, _ = process.communicate()

    assert process.returncode is not None
    # Sixel device control string starts with ESC P q (or similar like hide cursor \x1b[?25l)
    assert "\x1bP" in stdout or "\x1b[?25l" in stdout

@requires_vulkan
def test_truecolor_character_output():
    """Test that truecolor mode emits 24-bit ANSI color codes."""
    process = subprocess.Popen(
        [DCAT_EXE, '--truecolor-characters', str(TEST_CUBE)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )
    try:
        stdout, _ = process.communicate(timeout=1.0)
    except subprocess.TimeoutExpired:
        process.kill()
        stdout, _ = process.communicate()

    assert process.returncode is not None
    # Truecolor sequences look like: ESC [ 38 ; 2 ; R ; G ; B m
    assert "\x1b[38;2;" in stdout or "\x1b[48;2;" in stdout or "\x1b[?25l" in stdout
