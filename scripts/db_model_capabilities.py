#!/usr/bin/env python3
"""
SQLite Model Capabilities & Benchmark Directory Script.
Defines industry benchmarks (SWE-bench, HumanEval, LiveCodeBench, Terminal-Bench 2.1, BFCL, GPQA, LMSYS Arena),
stores model capabilities for active models (Antigravity, OpenCode Zen, OpenRouter),
and logs official vs estimated data sources in ~/.qcode.db.
"""

import sqlite3
import os
import time

DB_PATH = os.environ.get("QCODE_DB_PATH", os.path.expanduser("~/.qcode.db"))

def init_db():
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    
    # 1. Benchmark Definitions Table
    cursor.execute("DROP TABLE IF EXISTS benchmark_definitions;")
    cursor.execute("""
    CREATE TABLE benchmark_definitions (
        name TEXT PRIMARY KEY,
        full_name TEXT NOT NULL,
        evaluates TEXT NOT NULL,
        key_metric TEXT NOT NULL,
        why_it_matters TEXT NOT NULL
    );
    """)

    # 2. Model Capabilities & Specs Table
    cursor.execute("DROP TABLE IF EXISTS model_capabilities;")
    cursor.execute("""
    CREATE TABLE model_capabilities (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        provider TEXT NOT NULL,
        model_id TEXT NOT NULL UNIQUE,
        model_name TEXT NOT NULL,
        architecture_info TEXT,
        context_window INTEGER,
        output_limit INTEGER,
        tool_call_supported INTEGER,
        multi_turn_reliable INTEGER,
        verified_benchmark TEXT,
        data_source TEXT,
        rate_limit_category TEXT,
        recommended_for TEXT,
        updated_at INTEGER
    );
    """)

    conn.commit()
    conn.close()

def seed_data():
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()

    # === Industry Benchmark Glossary Data ===
    benchmarks = [
        ("SWE-bench Verified", "Software Engineering Benchmark (Human-Filtered)", "Resolving real GitHub issues in Python/C++ codebases", "% resolved issues", "Gold standard for full-repo agentic software engineering."),
        ("Terminal-Bench 2.1", "Terminal Command & Execution Benchmark", "CLI tool usage, shell scripting, multi-step terminal actions", "% task completion", "Measures agent performance when executing bash/shell tools."),
        ("BFCL", "Berkeley Function Calling Leaderboard", "JSON tool schema adherence, parameter extraction, function calling", "% valid tool calls", "Crucial for preventing infinite tool loops and parameter hallucinations."),
        ("LiveCodeBench", "Live Algorithmic Coding Benchmark", "Solving newly released LeetCode/Codeforces problems (uncensored)", "Pass@1 accuracy", "Prevents test set contamination; tests raw algorithmic problem solving."),
        ("HumanEval", "OpenAI HumanEval Benchmark", "Single-function Python code generation from docstrings", "Pass@1 accuracy", "Classic baseline for basic code syntax and function completion."),
        ("Aider Benchmark", "Aider Code Refactoring Benchmark", "Editing existing code files & applying unified diffs", "% refactoring pass rate", "Measures diff generation accuracy and code modification precision."),
        ("GPQA Diamond", "Google-Proof Q&A (Diamond Subset)", "Graduate-level scientific & technical reasoning", "Accuracy %", "Evaluates deep reasoning capacity beyond memorized knowledge."),
        ("LMSYS Arena ELO", "LMSYS Chatbot Arena", "Blind human side-by-side preference evaluation", "ELO score", "Reflects real user preference and conversational quality.")
    ]

    for b in benchmarks:
        cursor.execute("INSERT OR REPLACE INTO benchmark_definitions VALUES (?, ?, ?, ?, ?);", b)

    # === Active Model Specifications Data ===
    models_data = [
        ("antigravity", "claude-opus-4-6-thinking", "Claude Opus 4.6 Thinking", "Anthropic Reasoning Model", 200000, 8192, 1, 1, "SWE-bench Verified ~74.5%", "Anthropic Vendor Release", "Internal OAuth (Free)", "Complex Multi-File Refactoring & Architect Reasoning Tasks", int(time.time())),
        ("antigravity", "claude-sonnet-4-6", "Claude Sonnet 4.6", "Anthropic Frontier Model", 200000, 8192, 1, 1, "SWE-bench Verified ~72.7%", "Anthropic Vendor Release", "Internal OAuth (Free)", "Production Agentic Workflows & Autonomous Coding", int(time.time())),
        ("antigravity", "gemini-3.1-pro-low", "Gemini 3.1 Pro", "Google DeepMind Dense/MoE", 2000000, 65536, 1, 1, "Top Tier (>60% SWE-bench)", "Google DeepMind Spec", "Internal OAuth (Free)", "2M Context System Architecture & Large Repo Analysis", int(time.time())),
        ("antigravity", "gemini-3.7-flash-high", "Gemini 3.7 Flash", "Google DeepMind High-Efficiency", 1000000, 65536, 1, 1, "Optimized Flash Agentic", "Google DeepMind Spec", "Internal OAuth (Free)", "High-Speed Agent Tools & Web Search Workflows", int(time.time())),
        ("antigravity", "gemini-3.7-flash-low", "Gemini 3.7 Flash (Low)", "Google DeepMind High-Efficiency", 1000000, 65536, 1, 1, "Optimized Flash Agentic", "Google DeepMind Spec", "Internal OAuth (Free)", "Fast In-line Code Edits & Low Latency Tool Calls", int(time.time())),
        ("antigravity", "gemini-3.6-flash-high", "Gemini 3.6 Flash", "Google DeepMind High-Efficiency", 1000000, 65536, 1, 1, "Optimized Flash Agentic", "Google DeepMind Spec", "Internal OAuth (Free)", "High-Speed Agent Tools & Web Search Workflows", int(time.time())),
        ("antigravity", "gemini-3.6-flash-low", "Gemini 3.6 Flash (Low)", "Google DeepMind High-Efficiency", 1000000, 65536, 1, 1, "Optimized Flash Agentic", "Google DeepMind Spec", "Internal OAuth (Free)", "Fast In-line Code Edits & Low Latency Tool Calls", int(time.time())),
        ("antigravity", "gemini-3.5-flash-low", "Gemini 3.5 Flash", "Google DeepMind High-Efficiency", 1000000, 65536, 1, 1, "Near-Pro Coding Flash Tier", "Google DeepMind Spec", "Internal OAuth (Free)", "General Fast Agent Execution", int(time.time())),
        ("antigravity", "gemini-3-pro-high", "Gemini 3 Pro High", "Google DeepMind Pro Reasoning", 2000000, 8192, 1, 1, "2M Context Diagnostics", "Google DeepMind Spec", "Internal OAuth (Free)", "Long-Context Deep Reasoning & Architecture Diagnostics", int(time.time())),

        # OpenCode Zen (Keyless Free Tier)
        ("opencode", "nemotron-3-ultra-free", "Nemotron 3 Ultra (Free)", "NVIDIA 550B MoE (55B Active, Mamba-Transformer)", 1000000, 128000, 1, 1, "Frontier Orchestration / 1M Context", "NVIDIA Official Release & OpenRouter API", "Keyless Free Tier", "Massive Repository Context (1M) & Deep Reasoning", int(time.time())),
        ("opencode", "deepseek-v4-flash-free", "DeepSeek V4 Flash (Free)", "DeepSeek 284B MoE (13B Active)", 1000000, 65536, 1, 1, "High Function-Calling Fidelity", "DeepSeek Official Spec", "Keyless Free Tier", "Iterative Fast Multi-Turn Agent Tool Loops", int(time.time())),
        ("opencode", "laguna-s-2.1-free", "Laguna S 2.1 (Free)", "Poolside 118B MoE (8B Active)", 128000, 32768, 1, 1, "70.2% on Terminal-Bench 2.1", "Poolside Official Spec via OpenRouter API", "Keyless Free Tier", "Code Diffs & Precise File Modifications", int(time.time())),
        ("opencode", "north-mini-code-free", "North Mini Code (Free)", "Cohere 30B MoE (3B Active)", 256000, 64000, 1, 1, "Cohere Agentic Coding Debut", "Cohere Official Spec via OpenRouter API", "Keyless Free Tier", "Terminal Commands & Quick Shell Scripts", int(time.time())),
        ("opencode", "mimo-v2.5-free", "MiMo V2.5 (Free)", "Xiaomi MoE Instruction Model", 128000, 32768, 1, 0, "General Agentic Benchmark", "Xiaomi Official Spec via OpenRouter API", "Keyless Free Tier", "General Chat & Single-turn Instructions", int(time.time())),
        ("opencode", "big-pickle", "Big Pickle (Free)", "Community High-Payload Model", 512000, 32768, 1, 0, "Experimental High Context", "OpenCode Local Config", "Keyless Free Tier", "Large Text Ingestion & Bulk Payload Inspection", int(time.time())),

        # OpenRouter Free Models
        ("openrouter", "poolside/laguna-s-2.1:free", "Poolside Laguna S 2.1 (Free)", "Poolside 118B MoE (8B Active)", 262144, 32768, 1, 1, "70.2% on Terminal-Bench 2.1", "Poolside Official Spec via OpenRouter API", "Strict (20 req/min)", "Iterative Code Modifications & Diffs", int(time.time())),
        ("openrouter", "poolside/laguna-xs-2.1:free", "Poolside Laguna XS 2.1 (Free)", "Poolside 33B MoE (3B Active)", 262144, 32768, 1, 1, "Laguna XS.2 Successor", "Poolside Official Spec via OpenRouter API", "Strict (20 req/min)", "Lightweight Code Edits", int(time.time())),
        ("openrouter", "nvidia/nemotron-3-ultra-550b-a55b:free", "Nemotron 3 Ultra 550B (Free)", "NVIDIA 550B MoE (55B Active)", 1000000, 65536, 1, 1, "Frontier MoE 1M Context", "NVIDIA Spec via OpenRouter API", "Strict (20 req/min)", "Deep Reasoning & Large File Inspection", int(time.time())),
        ("openrouter", "nvidia/nemotron-3-super-120b-a12b:free", "Nemotron 3 Super 120B (Free)", "NVIDIA 120B MoE (12B Active)", 262144, 32768, 1, 1, "Hybrid Mamba-Transformer MoE", "NVIDIA Spec via OpenRouter API", "Strict (20 req/min)", "Multi-agent Compute Efficiency Tasks", int(time.time())),
        ("openrouter", "cohere/north-mini-code:free", "Cohere North Mini Code (Free)", "Cohere 30B MoE (3B Active)", 256000, 32768, 1, 1, "Cohere Debut Sparse MoE", "Cohere Spec via OpenRouter API", "Strict (20 req/min)", "Fast Command Execution & Auto-complete", int(time.time()))
    ]

    for row in models_data:
        cursor.execute("""
        INSERT INTO model_capabilities 
        (provider, model_id, model_name, architecture_info, context_window, output_limit, tool_call_supported, multi_turn_reliable, verified_benchmark, data_source, rate_limit_category, recommended_for, updated_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
        """, row)

    conn.commit()
    conn.close()

def display():
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    
    print("\n" + "=" * 125)
    print("1. KEY INDUSTRY BENCHMARKS EXPLAINED (Stored in ~/.qcode.db)")
    print("=" * 125)
    cursor.execute("SELECT name, evaluates, key_metric, why_it_matters FROM benchmark_definitions")
    for name, ev, met, why in cursor.fetchall():
        print(f"• {name:<20} | Evaluates: {ev:<65} | Metric: {met}")
        print(f"  Why it matters: {why}\n")

    print("=" * 125)
    print("2. ACTIVE MODEL CAPABILITIES TABLE")
    print("=" * 125)
    cursor.execute("""
    SELECT provider, model_id, architecture_info, context_window, multi_turn_reliable, verified_benchmark, recommended_for 
    FROM model_capabilities 
    ORDER BY provider, context_window DESC
    """)
    rows = cursor.fetchall()
    print(f"{'Provider':<11} | {'Model ID':<35} | {'Architecture / Active Params':<35} | {'Context':<7} | {'MultiTurn':<9} | {'Verified Spec / Metric':<28} | {'Recommended For'}")
    print("-" * 145)
    for prov, mid, arch, ctx, multiturn, bench, rec in rows:
        mt_str = "Reliable" if multiturn else "Unstable"
        ctx_str = f"{ctx//1000}K" if ctx < 1000000 else f"{ctx//1000000}M"
        print(f"{prov:<11} | {mid:<35} | {arch:<35} | {ctx_str:<7} | {mt_str:<9} | {bench:<28} | {rec}")
    print("=" * 145 + "\n")
    conn.close()

if __name__ == "__main__":
    init_db()
    seed_data()
    display()
