.PHONY: build build-tests run-tests run-jit-tests clean

build:
	cmake -B build
	make -C build -j

build-tests:
	make -C tests

run-tests: build build-tests
	./grade.sh

run-jit-tests: build
	ctest --test-dir build --output-on-failure -R '^jit_.*test$$'

clean:
	rm -r build
	make -C tests clean
