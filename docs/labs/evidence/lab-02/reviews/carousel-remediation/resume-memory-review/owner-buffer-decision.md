# Draw-buffer height decision record

Owner clarification, 2026-10-13: 50 lines was chosen as a mitigation for the
LAB-02 draw-buffer allocation failure. No other buffer-height value was tried
when it was chosen. It is not a tuned optimum or an approved performance budget.
The owner confirms the carousel is now visible after build/flash.

Revisit this choice in C5/C6 if draw-buffer size materially affects memory
headroom or rendering: compare candidate heights on the same fixture/build
variant and kit, recording largest DMA-capable free block before allocation,
allocation bytes, sustained render/input distributions, fragmentation and
visual continuity. Preserve the current working value until evidence supports
a change.
