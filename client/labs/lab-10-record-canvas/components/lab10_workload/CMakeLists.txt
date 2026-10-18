# L10-06 is a bounded deterministic workload source.  It is compiled into
# the firmware dependency graph for toolchain coverage, but the app does not
# execute it and it makes no hardware timing or audio claim.
idf_component_register(
    SRCS "lab10_workload.c"
    INCLUDE_DIRS "include"
)
