# Build a library allowing FFI access to the Wasm spec interpreter.

OCAML_FLAGS := -g -keep-locs -runtime-variant _pic
# By default, we build in a sub-directory but we can override this with `make
# BUILD_DIR=...`.
BUILD_DIR := _build
# Currently the WebAssembly spec interpreter is buried in a Git submodule as is
# its build directory, `_build`. Cargo may not like that files are changing
# outside of `target` (TODO).
SPEC_DIR := spec/interpreter
SPEC_BUILD_DIR := $(SPEC_DIR)/_build
SPEC_LIB := $(SPEC_BUILD_DIR)/wasm.cmxa


# Build and package the static library, `libinterpret.a`.
$(BUILD_DIR)/libinterpret.a: $(BUILD_DIR)/interpret.lib.o
	ar qs $@ $^
$(BUILD_DIR)/interpret.lib.o: $(SPEC_LIB) $(BUILD_DIR)/interpret.cmx
	ocamlopt $(OCAML_FLAGS) -I $(SPEC_BUILD_DIR) -o $@ -output-complete-obj $^
$(BUILD_DIR)/interpret.cmx: interpret.ml $(SPEC_BUILD_DIR) $(BUILD_DIR)
	ocamlopt $(OCAML_FLAGS) -I $(SPEC_BUILD_DIR) -o $@ -c -impl $<
$(BUILD_DIR):
	mkdir -p $@


# We also need to be able to build the spec's `wasm.cmxa`.
$(SPEC_LIB):
	make -C $(SPEC_DIR) libopt


clean:
	rm -rf $(BUILD_DIR)
	make -C $(SPEC_DIR) clean
