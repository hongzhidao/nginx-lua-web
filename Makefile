.DEFAULT_GOAL := test

.PHONY: test

test:
	@sh tests/run.sh
