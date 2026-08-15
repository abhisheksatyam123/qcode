#!/usr/bin/env python3
# /// script
# requires-python = ">=3.8"
# dependencies = [
#     "click>=8.1.0",
#     "rich>=13.0.0",
# ]
# ///
"""Build script for QCode

Usage: 
    uv run scripts/build.py [OPTIONS]

Examples:
    uv run scripts/build.py --mode debug
    uv run scripts/build.py --mode release --tests
    uv run scripts/build.py --mode debug --tests --clean --export-compile-commands

This script handles:
- CMake configuration with git submodule dependencies
- Building in Debug or Release mode
- Optional test building
- Clean builds
- Cross-platform support
- Export compile commands for IDEs
"""

import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Optional

import click
from rich.console import Console
from rich.panel import Panel
from rich.progress import Progress, SpinnerColumn, TextColumn
from rich.table import Table

console = Console()


def run_command(cmd: list[str], cwd: Optional[Path] = None, check: bool = True) -> subprocess.CompletedProcess:
    """Run a command and handle errors with rich output."""
    console.print(f"[dim]Running:[/dim] [cyan]{' '.join(cmd)}[/cyan]")
    
    try:
        result = subprocess.run(cmd, cwd=cwd, check=check, capture_output=True, text=True)
        if result.stdout.strip():
            console.print(f"[dim]{result.stdout.strip()}[/dim]")
        return result
    except subprocess.CalledProcessError as e:
        console.print(f"[red]Error running command:[/red] {e}")
        if e.stderr:
            console.print(f"[red]Error output:[/red] {e.stderr}")
        if e.stdout:
            console.print(f"[yellow]Output:[/yellow] {e.stdout}")
        sys.exit(1)


def check_uv():
    """Check if uv is available."""
    try:
        subprocess.run(["uv", "--version"], check=True, capture_output=True)
        return True
    except (subprocess.CalledProcessError, FileNotFoundError):
        return False


def check_node():
    """Check if node/npm is available for webui build."""
    try:
        subprocess.run(["node", "--version"], check=True, capture_output=True)
        subprocess.run(["npm", "--version"], check=True, capture_output=True)
        return True
    except (subprocess.CalledProcessError, FileNotFoundError):
        return False


def build_webui(project_root: Path, build_dir: Path):
    """Build the WebUI frontend."""
    webui_src = project_root / "apps" / "webui"
    webui_dist = build_dir / "webui" / "dist"
    
    if not (webui_src / "package.json").exists():
        console.print("[yellow]WebUI package.json not found, skipping WebUI build[/yellow]")
        return False
    
    console.print("[bold blue]Building WebUI...[/bold blue]")
    
    # Install dependencies
    with Progress(
        SpinnerColumn(),
        TextColumn("[progress.description]{task.description}"),
        console=console,
    ) as progress:
        task = progress.add_task("Installing npm dependencies...", total=None)
        result = subprocess.run(["npm", "ci"], cwd=webui_src, capture_output=True, text=True)
        if result.returncode != 0:
            console.print(f"[red]npm ci failed:[/red] {result.stderr}")
            return False
        progress.update(task, completed=True)
    
    # Build
    with Progress(
        SpinnerColumn(),
        TextColumn("[progress.description]{task.description}"),
        console=console,
    ) as progress:
        task = progress.add_task("Building WebUI...", total=None)
        result = subprocess.run(["npm", "run", "build"], cwd=webui_src, capture_output=True, text=True)
        if result.returncode != 0:
            console.print(f"[red]npm build failed:[/red] {result.stderr}")
            return False
        progress.update(task, completed=True)

    # Vendor marked + js-yaml into standalone IIFE bundles served as
    # /vendor-marked.min.js and /vendor-jsyaml.min.js (offline-safe).
    vendor_cmd = [
        "npx", "esbuild",
        "--bundle", "--minify",
        "--format=iife", "--global-name=marked",
        "--outfile=" + str(webui_src / "src" / "vendor-marked.min.js"),
        str(webui_src / "node_modules" / "marked" / "lib" / "marked.esm.js"),
    ]
    subprocess.run(vendor_cmd, cwd=webui_src, check=True, capture_output=True, text=True)
    vendor_cmd = [
        "npx", "esbuild",
        "--bundle", "--minify",
        "--format=iife", "--global-name=jsyaml",
        "--outfile=" + str(webui_src / "src" / "vendor-jsyaml.min.js"),
        str(webui_src / "node_modules" / "js-yaml" / "dist" / "js-yaml.mjs"),
    ]
    subprocess.run(vendor_cmd, cwd=webui_src, check=True, capture_output=True, text=True)

    # Copy to build directory + sync raw src (incl. vendor bundles) next to the server.
    if (webui_src / "dist").exists():
        webui_dist.parent.mkdir(parents=True, exist_ok=True)
        if webui_dist.exists():
            shutil.rmtree(webui_dist)
        shutil.copytree(webui_src / "dist", webui_dist)
        # Vendored bundles live outside the vite pipeline; copy into dist too.
        for v in ("vendor-marked.min.js", "vendor-jsyaml.min.js"):
            vp = webui_src / "src" / v
            if vp.exists():
                shutil.copy2(vp, webui_dist / v)
        server_webui = build_dir / "apps" / "server" / "webui"
        if (webui_src / "src").exists():
            server_webui.mkdir(parents=True, exist_ok=True)
            for item in (webui_src / "src").glob("*"):
                if item.is_file():
                    shutil.copy2(item, server_webui / item.name)
        console.print(f"[green]✓[/green] WebUI built to {webui_dist}")
        return True
    return False


@click.command()
@click.option(
    "--mode", 
    type=click.Choice(["debug", "release"], case_sensitive=False),
    default="debug",
    help="Build configuration (debug or release)"
)
@click.option(
    "--tests", 
    is_flag=True,
    help="Enable building tests"
)
@click.option(
    "--clean", 
    is_flag=True,
    help="Clean build directory before building"
)
@click.option(
    "--verbose", 
    is_flag=True,
    help="Enable verbose build output"
)
@click.option(
    "--export-compile-commands",
    is_flag=True,
    help="Export compile commands for IDEs (compile_commands.json)"
)
@click.option(
    "--jobs",
    type=int,
    default=None,
    help="Number of parallel build jobs (default: CPU count)"
)
@click.option(
    "--no-tui",
    is_flag=True,
    help="Skip TUI build"
)
@click.option(
    "--no-server",
    is_flag=True,
    help="Skip server build"
)
@click.option(
    "--no-cli",
    is_flag=True,
    help="Skip CLI build"
)
@click.option(
    "--no-webui",
    is_flag=True,
    help="Skip WebUI build"
)
def main(mode: str, tests: bool, clean: bool, verbose: bool, export_compile_commands: bool, 
         jobs: Optional[int], no_tui: bool, no_server: bool, no_cli: bool, no_webui: bool):
    """Build QCode with modern tooling."""
    
    # Get project paths
    script_dir = Path(__file__).parent
    project_root = script_dir.parent
    build_dir = project_root / "build"
    
    # Display build configuration
    config_table = Table(title="Build Configuration", show_header=True, header_style="bold blue")
    config_table.add_column("Setting", style="cyan")
    config_table.add_column("Value", style="green")
    
    config_table.add_row("Project root", str(project_root))
    config_table.add_row("Build directory", str(build_dir))
    config_table.add_row("Build mode", mode.upper())
    config_table.add_row("With tests", "✓" if tests else "✗")
    config_table.add_row("Clean build", "✓" if clean else "✗")
    config_table.add_row("Export compile commands", "✓" if export_compile_commands else "✗")
    config_table.add_row("Parallel jobs", str(jobs or os.cpu_count() or 4))
    config_table.add_row("Build TUI", "✗" if no_tui else "✓")
    config_table.add_row("Build Server", "✗" if no_server else "✓")
    config_table.add_row("Build CLI", "✗" if no_cli else "✓")
    config_table.add_row("Build WebUI", "✗" if no_webui else "✓")
    
    console.print(config_table)
    console.print()
    
    # Check uv
    if not check_uv():
        console.print("[red]Error: 'uv' not found. Please install uv from https://github.com/astral-sh/uv[/red]")
        sys.exit(1)
    
    # Clean build directory if requested
    if clean and build_dir.exists():
        with Progress(
            SpinnerColumn(),
            TextColumn("[progress.description]{task.description}"),
            console=console,
        ) as progress:
            task = progress.add_task("Cleaning build directory...", total=None)
            shutil.rmtree(build_dir)
            progress.update(task, completed=True)
        console.print("[green]✓[/green] Build directory cleaned")
    
    # Create build directory
    build_dir.mkdir(exist_ok=True)
    
    # Build WebUI first (if requested)
    if not no_webui and check_node():
        console.print()
        build_webui(project_root, build_dir)
    elif not no_webui:
        console.print("[yellow]Node.js not found, skipping WebUI build[/yellow]")
    
    # Configure CMake
    cmake_args = [
        "cmake",
        "-B", str(build_dir),
        "-S", str(project_root),
        f"-DCMAKE_BUILD_TYPE={mode.capitalize()}",
        f"-DBUILD_TESTS={'ON' if tests else 'OFF'}",
        f"-DCMAKE_EXPORT_COMPILE_COMMANDS={'ON' if export_compile_commands else 'OFF'}",
        f"-DQCODE_BUILD_TUI={'OFF' if no_tui else 'ON'}",
        f"-DQCODE_BUILD_SERVER={'OFF' if no_server else 'ON'}",
        f"-DQCODE_BUILD_CLI={'OFF' if no_cli else 'ON'}",
    ]
    
    console.print("[bold blue]Configuring with CMake...[/bold blue]")
    run_command(cmake_args, cwd=project_root)
    
    # Build
    build_args = ["cmake", "--build", str(build_dir)]
    if jobs:
        build_args.extend(["--parallel", str(jobs)])
    if verbose:
        build_args.append("--verbose")
    
    console.print("[bold blue]Building...[/bold blue]")
    run_command(build_args, cwd=project_root)
    
    # Copy compile_commands.json to project root if requested
    if export_compile_commands:
        compile_commands = build_dir / "compile_commands.json"
        if compile_commands.exists():
            shutil.copy2(compile_commands, project_root / "compile_commands.json")
            console.print("[green]✓[/green] compile_commands.json copied to project root")
    
    # Success message
    console.print()
    console.print(Panel.fit(
        f"[green]Build successful![/green]\n\n"
        f"Binaries in: [cyan]{build_dir}[/cyan]",
        title="✓ Complete",
        border_style="green"
    ))
    
    # List built targets
    console.print("[bold]Built targets:[/bold]")
    targets = ["qcode-tui", "qcode-server", "qcode-cli"]
    for target in targets:
        binary = build_dir / "apps" / target.replace("qcode-", "") / target
        if not binary.exists():
            binary = build_dir / "src" / target.replace("qcode-", "") / target
        if binary.exists():
            console.print(f"  ✓ {target} -> {binary}")
        else:
            console.print(f"  ✗ {target} (not found)")


if __name__ == "__main__":
    main()
