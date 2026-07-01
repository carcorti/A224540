CC := gcc
BUILD_DIR ?= ../build
RUN_ROOT ?= ../runs
ifndef RUN_ID
# GNU coreutils date with %N support is expected on the target Linux system.
RUN_ID := $(shell date -u +%Y%m%dT%H%M%S%NZ)
endif
RUN_DIR ?= $(RUN_ROOT)/a224540_$(RUN_ID)

COMMON_WARNINGS := -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wdouble-promotion -Wformat=2
OPT_FLAGS ?= -O3 -march=native -flto -funroll-loops -fomit-frame-pointer -DNDEBUG
CFLAGS ?= -std=c17 $(OPT_FLAGS) $(COMMON_WARNINGS)
CPPFLAGS += '-DA224540_BUILD_CFLAGS="$(CFLAGS)"'
LDFLAGS ?=
LDLIBS ?=

TARGET := $(BUILD_DIR)/a224540
SAN_TARGET := $(BUILD_DIR)/a224540_san
SRC := a224540.c
BFILE := ../data/b224540.txt
SMALL_TOPDOWN := ../tools/a224540_small_topdown.py
AWK ?= awk

.DELETE_ON_ERROR:

.PHONY: all clean self-test replay calibrate n22 run-dir checksum small-topdown cli-test sanitize segment-validate aggregate-adversarial validate-core validate-full validate test

all: $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

run-dir:
	mkdir -p $(RUN_DIR)

$(TARGET): $(SRC) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS) $(LDLIBS)

self-test: $(TARGET)
	$(TARGET) --self-test

replay: $(TARGET) run-dir
	$(TARGET) --replay-bfile $(BFILE) --tsv $(RUN_DIR)/replay.tsv --manifest $(RUN_DIR)/manifest_replay.txt

calibrate: $(TARGET) run-dir
	$(TARGET) --calibrate 18 21 --tsv $(RUN_DIR)/calibration_18_21.tsv --manifest $(RUN_DIR)/manifest_calibration_18_21.txt

n22: $(TARGET) run-dir
	$(TARGET) --n 22 --tsv $(RUN_DIR)/a22.tsv --manifest $(RUN_DIR)/manifest_a22.txt
	$(MAKE) checksum RUN_DIR=$(RUN_DIR)

small-topdown: run-dir
	test -f $(SMALL_TOPDOWN)
	python3 $(SMALL_TOPDOWN) --bfile $(BFILE) --max-n 14 --tsv $(RUN_DIR)/small_topdown_n0_14.tsv

cli-test: $(TARGET)
	! $(TARGET)
	! $(TARGET) --bogus
	! $(TARGET) --n
	! $(TARGET) --n +1
	! $(TARGET) --n " 1"
	! $(TARGET) --n 41
	! $(TARGET) --n 3 --n 4
	! $(TARGET) --self-test --n 3
	! $(TARGET) --self-test --tsv /tmp/a224540_self.tsv
	! $(TARGET) --calibrate 5 4
	! $(TARGET) --calibrate 40 41 --tsv /tmp/a224540_bad_cal.tsv
	! $(TARGET) --range 5 4
	! $(TARGET) --range 3 4 --calibrate 3 4
	! $(TARGET) --n 3 --tsv /tmp/a224540_same.tsv --manifest /tmp/a224540_same.tsv
	! $(TARGET) --replay-bfile $(BFILE) --tsv $(BFILE)
	! $(TARGET) --plan 14 8 0
	! $(TARGET) --segment 14 8 3 2
	! $(TARGET) --aggregate
	! $(TARGET) --segment-check 14 8 0

sanitize: $(SRC) | $(BUILD_DIR) run-dir
	$(CC) $(CPPFLAGS) -std=c17 -O1 -g3 $(COMMON_WARNINGS) -fsanitize=address,undefined -fno-omit-frame-pointer $(SRC) -o $(SAN_TARGET) $(LDFLAGS) -fsanitize=address,undefined $(LDLIBS)
	env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=0 $(SAN_TARGET) --self-test
	env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=0 $(SAN_TARGET) --n 14 --tsv $(RUN_DIR)/san_n14.tsv --manifest $(RUN_DIR)/manifest_san_n14.txt

segment-validate: $(TARGET) run-dir
	$(TARGET) --segment-check 14 8 2 --tsv $(RUN_DIR)/segment_check_n14.tsv --manifest $(RUN_DIR)/manifest_segment_check_n14.txt
	$(TARGET) --plan 14 8 2 --tsv $(RUN_DIR)/plan_n14_d8.tsv --manifest $(RUN_DIR)/manifest_plan_n14_d8.txt
	$(TARGET) --segment 14 8 0 1 --tsv $(RUN_DIR)/segment_n14_0_1.tsv --manifest $(RUN_DIR)/manifest_segment_n14_0_1.txt
	$(TARGET) --segment 14 8 2 3 --tsv $(RUN_DIR)/segment_n14_2_3.tsv --manifest $(RUN_DIR)/manifest_segment_n14_2_3.txt
	$(TARGET) --aggregate $(RUN_DIR)/segment_n14_2_3.tsv $(RUN_DIR)/segment_n14_0_1.tsv --tsv $(RUN_DIR)/aggregate_n14.tsv --manifest $(RUN_DIR)/manifest_aggregate_n14.txt

aggregate-adversarial: $(TARGET) segment-validate
	$(AWK) 'BEGIN{FS=OFS="\t"} NR==1 {print; next} {$$2=22; print}' $(RUN_DIR)/segment_n14_0_1.tsv > $(RUN_DIR)/bad_nm_segment_n14_0_1.tsv
	$(AWK) 'BEGIN{FS=OFS="\t"} NR==1 {print; next} {$$2=22; print}' $(RUN_DIR)/segment_n14_2_3.tsv > $(RUN_DIR)/bad_nm_segment_n14_2_3.tsv
	! $(TARGET) --aggregate $(RUN_DIR)/bad_nm_segment_n14_0_1.tsv $(RUN_DIR)/bad_nm_segment_n14_2_3.tsv --tsv $(RUN_DIR)/bad_nm_aggregate.tsv --manifest $(RUN_DIR)/manifest_bad_nm_aggregate.txt
	$(AWK) 'NR==1 {print; next} {print $$0 "\tEXTRA_FIELD"}' $(RUN_DIR)/segment_n14_0_1.tsv > $(RUN_DIR)/extra_field_segment_n14_0_1.tsv
	! $(TARGET) --aggregate $(RUN_DIR)/extra_field_segment_n14_0_1.tsv $(RUN_DIR)/segment_n14_2_3.tsv --tsv $(RUN_DIR)/extra_field_aggregate.tsv --manifest $(RUN_DIR)/manifest_extra_field_aggregate.txt
	$(AWK) 'BEGIN{FS=OFS="\t"} NR==1 {print; next} {$$2=4294967296; print}' $(RUN_DIR)/segment_n14_0_1.tsv > $(RUN_DIR)/bad_n_domain_segment_n14_0_1.tsv
	! $(TARGET) --aggregate $(RUN_DIR)/bad_n_domain_segment_n14_0_1.tsv --tsv $(RUN_DIR)/bad_n_domain_aggregate.tsv --manifest $(RUN_DIR)/manifest_bad_n_domain_aggregate.txt
	printf 'mode\tn\tM\tdepth\tfirst_index\tlast_index\tprefix_count\tfrontier_count\troots_processed\tcount\tstatus\telapsed_sec\tnodes_per_sec\tmax_stack_len\tmax_value\tdouble_pushes\todd_pushes\nsegment\t22\t31381059609\t0\t0\t0\t0\t1\t1\t123456789\tSEGMENT\t0.001\t123\t1\t1\t0\t0\n' > $(RUN_DIR)/fake_unknown_segment_n22.tsv
	$(TARGET) --aggregate $(RUN_DIR)/fake_unknown_segment_n22.tsv --tsv $(RUN_DIR)/fake_unknown_aggregate.tsv --manifest $(RUN_DIR)/manifest_fake_unknown_aggregate.txt
	grep -q 'AGGREGATED_NO_ORACLE' $(RUN_DIR)/fake_unknown_aggregate.tsv

checksum: run-dir
	sha256sum $(SRC) Makefile $(BFILE) > $(RUN_DIR)/sha256sums.txt
	find $(RUN_DIR) -maxdepth 1 -type f \( -name '*.tsv' -o -name 'manifest_*.txt' \) -print0 | sort -z | xargs -0 -r sha256sum >> $(RUN_DIR)/sha256sums.txt

validate-core: self-test cli-test replay calibrate segment-validate aggregate-adversarial

validate-full: validate-core small-topdown
	$(MAKE) checksum RUN_DIR=$(RUN_DIR)

validate: validate-full

test: validate

clean:
	rm -f $(TARGET) $(SAN_TARGET)
