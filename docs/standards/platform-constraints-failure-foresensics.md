Family Link — Platform Constraints and Failure Forensics
You are conducting a read-only forensic investigation of the Family Link ESP firmware project.
The objective is to determine why implementations repeatedly fail on actual hardware despite successful agent development, compilation, or testing.
Do not modify code, implement fixes, or launch implementation subagents.
Investigation
1. Identify the exact ESP chip, board configuration, display hardware, ESP-IDF/Arduino dependencies, LVGL version, and memory configuration from project evidence.
2. Establish the hardware and software constraints using authoritative platform documentation. Do not assume that available PSRAM is interchangeable with internal DRAM.
3. Inspect memory allocation patterns, LVGL integration, DMA requirements, task stacks, synchronization, network operations, and object lifetimes.
4. Locate available historical Cursor and Codex agent logs, build failures, crash reports, and repair attempts. Do not assume all historical logs are accessible.
5. Correlate recurring failures with source changes and attempted repairs.
6. Identify implementation assumptions that were incorrect, unsupported, or insufficiently tested.
7. Distinguish confirmed defects, likely risks, and unverified hypotheses.
Required deliverables
A. Platform Capability Profile
Document verified hardware capabilities, memory regions, relevant configuration, and hard restrictions.
B. Historical Failure Analysis
Group recurring failures by cause. Include evidence, attempted repairs, outcomes, and recurrence where established.
C. Resource and Concurrency Map
Identify important allocations, ownership, task boundaries, shared resources, and potential conflicts.
D. Proposed Engineering Guardrails
Recommend specific automated checks, instrumentation, tests, and hardware acceptance criteria that would prevent or detect these failures.
For each recommendation, explain what historical or current failure it addresses, how it would be validated, and whether it requires actual hardware.
Evidence standards
- Cite exact files, configuration values, logs, or documentation.
- Never present suspected causes as established facts.
- Explicitly distinguish a successful build from successful execution on hardware.
- Identify missing evidence and measurements.
- Prefer reproducible tests and instrumentation over additional written rules.
- Do not prescribe large architectural rewrites without evidence.
- Prioritize findings by severity, recurrence, and preventability.
Execution constraints
Work in bounded investigative stages. Do not create a large implementation workflow.
Produce an initial evidence inventory and findings report before conducting deeper investigations.
Stop when the evidence supports actionable conclusions or when additional investigation requires unavailable logs or physical hardware.
Success criterion: Another implementation agent should be able to use the findings and proposed guardrails to avoid repeating known platform failures.