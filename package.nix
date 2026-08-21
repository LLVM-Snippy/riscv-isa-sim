let
  spikeVersion = builtins.head (
    builtins.match ".*SPIKE_VERSION \"(.*)\".*" (builtins.readFile ./VERSION)
  );
in

{
  stdenv,
  lib,
  cmake,
  pkg-config,
  lit,
  rvmi,
  dtc,
  autoreconfHook,
}:

stdenv.mkDerivation (finalAttrs: {
  pname = "riscv-isa-sim";
  version = spikeVersion;

  src = lib.fileset.toSource {
    root = ./.;
    fileset = lib.fileset.unions [
      ./.scripts/CMakeLists.txt
      ./Makefile.in
      ./VERSION
      ./aclocal.m4
      ./configure
      ./configure.ac
      ./customext
      ./disasm
      ./fdt
      ./fesvr
      ./m4
      ./riscv
      ./riscv-disasm.pc.in
      ./riscv-fesvr.pc.in
      ./riscv-riscv.pc.in
      ./sc-rvm
      ./scripts/mk-install-dirs.sh
      ./scripts/vcs-version.sh
      ./softfloat
      ./spike_dasm
      ./spike_main
      ./debug_rom
    ];
  };

  autoreconfFlags = [
    "../"
    "--install"
    "--verbose"
    "--force"
  ];

  preAutoreconf = ''
    chmod +w ../. ../scripts
    patchShebangs ../scripts
  '';

  sourceRoot = "source/.scripts";

  outputs = [
    "out"
  ];

  cmakeFlags = [
    (lib.cmakeBool "BUILD_TESTING" finalAttrs.finalPackage.doCheck)
    (lib.cmakeFeature "MODEL_LAUNCHER_TESTS_DIR" "${rvmi.out}/share/testgen-model-interface/rvm")
    (lib.cmakeFeature "CMAKE_MODULE_PATH" "${rvmi.dev}/lib/cmake/testgen-model-interface")
  ];

  nativeBuildInputs = [
    cmake
    dtc
    pkg-config
    autoreconfHook
    rvmi
  ];

  strictDeps = true;

  buildInputs = [
    rvmi
  ];

  nativeCheckInputs = [
    lit
  ];

  # TODO: Can't run tests for now because we need cross clang/gcc stdenvs for
  # freestanding riscv32 and riscv64.
  doCheck = false;

  meta.mainProgram = "spike";
})
