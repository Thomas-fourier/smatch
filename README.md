

# Execute the artefact

## Get the Linux kernel code
Clone the github mirror of Linux at version 6.15.
```
git clone https://github.com/torvalds/linux -bv6.15
```

## Build the container
Clone the repo of our checker: <span style="color:red">TODO: Replace URL and dirname.</span>.
```
git clone https://github.com/thomas-fourier/smatch_private -binterface/check-generic-args-pairs
cd smatch_private
```

This command creates a debian container, installs all the Smatch dependencies, and builds Smatch with the modifications we made.
```
docker build -t smatch .
```

Run the docker container. Replace  `PATH_TO_LINUX` with the relative path of the Linux kernel you cloned above (e.g.: `../linux`).
```
docker run -it --rm -d PATH_TO_LINUX:/data/linux bash
```

You will be dumped in a shell in the container, and in the Linux folder.

## Running the artefact


### Configure the linux kernel

You must first configure the kernel. This can be achieve automatically with 
```
make allyesconfig
```
This is the option we used for the testing. Other Linux options are available with `make help`

### Execute Smatch entirely automatically
Simply run:
```
run_all_steps
```
This command takes about 1 hour.

## Expected results

The reports are generated in the `error` folder.
It contains a file for each interface and all the generated reports.
We consider that a report is a set of warnings related to the same file and interface.

The next step is to sort the reports between the true positives (actual bugs) and
false positives.
We have sorted the reports in the `artefact/errors_artefact` folder in this repository. This folder contains:
  * `true_positives` with the reports and the corresponding hash of bug fixes or email archive for the bug patches that were not mainlined,
  * `false_positives` with the reports that we considered as false positives
  * `not_sorted` for all the reports.

# Patched bugs

## Upstreamed bug-fixes:

Here is the list of the patches that were submitted and mainlined. They are present in https://github.com/torvalds/linux in versions after v6.15.

To see the commit, run:
```
git show fcd431a9627f
```

| Patch | Commit header |
|----------|-----------------------------------------------------------------------|
| fcd431a9627f | RDMA/bnxt_re: fix dma_free_coherent() pointer |
| 1461209cf813 | platform/x86: msi-laptop: add missing sysfs_remove_group()  |
| 838d2d51513e | crypto: aspeed - Fix dma_unmap_sg() direction |
| f5d643156ef6 | crypto: atmel - Fix dma_unmap_sg() direction  |
| 21140e5caf01 | crypto: rockchip - Fix dma_unmap_sg() nents value |
| 8ab2f1c35669 | mmc: mvsdio: Fix dma_unmap_sg() nents value |
| edb35b1ffc68 | scsi: myrs: Fix dma_alloc_coherent() error check  |
| 3b36f86dc472 | mtd: rawnand: rockchip: Add missing check after DMA map |
| 79e441ee4794 | mtd: rawnand: renesas: Add missing check after DMA map |
| c6ee78fc8f3e | dmaengine: nbpfaxi: Add missing check after DMA map |
| 3a988d0b65d7 | scsi: elx: efct: Fix dma_unmap_sg() nents value |
| 063bec4444d5 | scsi: isci: Fix dma_unmap_sg() nents value  |
| 023a293b9cd0 | scsi: ibmvscsi_tgt: Fix dma_unmap_sg() nents value  |
| 34b283636181 | crypto: img-hash - Fix dma_unmap_sg() nents value |
| 96a1e15e6021 | net: ag71xx: Add missing check after DMA map  |
| 1069a4fe637d | media: pci: ivtv: Add missing check after DMA map |
| 23b53639a793 | media: cx18: Add missing check after DMA map  |
| cb7fa6b6fc71 | crypto: inside-secure - Fix `dma_unmap_sg()` nents value  |
| ff09b71bf9da | mmc: bcm2835: Fix dma_unmap_sg() nents value  |
| 1db50f7b7a79 | Fix dma_unmap_sg() nents value  |
| c4890963350d | atm: idt77252: Add missing `dma_map_error()`  |
| 00f452a1b084 | scsi: qla4xxx: Fix missing DMA mapping error in qla4xxx_alloc_pdu() |
| 85d6fbc47c30 | scsi: fnic: Fix missing DMA mapping error in fnic_send_frame()  |
| b6cd3cfb5afe | crypto: sun8i-ce - fix nents passed to dma_unmap_sg() |
| c48c0fd0e196 | firewire: nosy: Fix dma_free_coherent() size  |
| 0e13150c1a13 | HID: Intel-thc-hid: Intel-thc: fix dma_unmap_sg() nents value |
| bb97131fbf9b | wifi: ath12k: fix dma_free_coherent() pointer |
| 9282a1e171ad | wifi: ath10k: fix dma_free_coherent() pointer |
| 4d984b0574ff | atm: Fix dma_free_coherent() size |
| b5c23a4d291d | auxdisplay: arm-charlcd: fix release_mem_region() size  |
| 941676c30ba5 | crypto: cavium - fix dma_free_coherent() size |
| 624a6760bf84 | crypto: octeontx - fix dma_free_coherent() size |
| 56bd3c0f749f | scsi: qla2xxx: edif: Fix dma_free_coherent() size |
| 36bd7d5deef9 | net: wan/fsl_ucc_hdlc: Fix dma_free_coherent() in uhdlc_memclean()  |
| ffe68c376699 | net: ethernet: ec_bhf: Fix dma_free_coherent() dma handle |
| 1ee57ab93b75 | crypto: hisilicon - Fix dma_unmap_single() direction  |


## Confirmed bug fixes

The three remaining patches that are under review are archived on [lore.kernel.org](https://lore.kernel.org). 

https://lore.kernel.org/all/20250623122557.116906-2-fourier.thomas@gmail.com/

https://lore.kernel.org/all/20260330154236.98665-2-fourier.thomas@gmail.com/

https://lore.kernel.org/all/20260330154236.98665-2-fourier.thomas@gmail.com/


# Additional information  

## Intermediary information

When running Smatch, multiple files may be generated.  
  * `smatch.log` which contains the logs of Smatch including function call for inference.
  * `smatch.warning` which contains the warnings generated by Smatch.
  * `*.pkl` which is used as a cache for inference.
  * `generated_spec`, a folder containing all the generated DSL files for each interface.
  * `.run_smatch_again`, an empty file that indicates that a wrapper has been found 
  * `errors`, a folder containing, for each interface, the reports raised.



We provide several files, in the `artefact` folder that are generated by the tool:
  * `generated_spec` contains the DSL files as initially generated,
  * `generated_spec_with_wrappers` contains the DSL files after all the wrappers have been found

## Additional scripts

All the commands presented in this section are available in the container. Start the container as presented above.

### Run the Smatch step-by-step
The `run_all_steps` script does multiple passes of Smatch to infer the rules, and run the checker iteratively until no more wrapper is found.
It runs Smatch multiple times as presented here:

First, retrieve the information for interface detection
```
SMATCH_ARG="--resemblance" run_all_kernel
```
This command usually takes about 30 minutes and dumps a lot of logs.

Use the gathered information to generate the specification files:
```
compute_function_pair_scores.py --log smatch.log --output generated_spec
```

Then, you can run the checker with the API specifications:
```
SMATCH_ARG="--check-api --wrappers" API_SPECS="generated_spec" run_all_kernel
```
This command takes about 30 minutes as well.

All the warnings are put in the `smatch.warning` file.  If a wrapper was found, the `.run_smatch_again` file is generated.
You can remove the `--wrappers` options to disable wrapper detection, or replace
`generated_spec` with any folder containing specification files. 

### Run the checker on a single file

To run the checker on a single file, run it with the makefile of Linux, after replacing
`SPEC_FILE` with a specification file:
```
make -k --output-sync CHECK="smatch -p=kernel --spammy --file-output --succeed --generic-arg-file=SPEC_FILE --check-api --wrappers" C=2 path/to/file.o
```

# Code structure

The artefact contains all the code for Smatch with minor modifications
(to fix bugs, to add helpers, etc.)

The main code we created reside in the checkers we created and the scripts :
  * `check_api_resemblance.c` which extracts API call information,
  * `smatch_scripts/compute_function_pair_scores.py`, which takes the information and creates the DSL files,
  * `dsl.[ch]` which parses the DSL,
  * `check_generic_args.c` which verifies that the code complies with the specifications,
  * `stringification.[ch]` which handles the standardization of the expression and their conversion to strings,
  * `check_search_wrappers.c` which flags the potential wrappers.

