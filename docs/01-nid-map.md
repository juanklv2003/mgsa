# 01 - Import map (HLE surface)

Derived from `BOOT.BIN` (plain ELF `mgp_main`): `.lib.stub` entries plus `.rodata.sceNid`.
Names resolved against the public PPSSPP HLE tables.
`???` marks an unresolved NID or a Konami-private function (expected for `zlibdec`, `sound`, `kjfs`).

**121 imports across 20 libraries** - 108 Sony NIDs + 13 Konami-private.

## ISA density in `.text` (`mgp_main`, 310,790 instructions)

| Class | Count | Share |
|---|---|---|
| COP2 (VFPU) | 1,078 | 0.35% |
| COP1 (FPU) | 18,689 | 6.01% |
| nop | 6,848 | 2.20% |

## `sceCtrl` - 2 function(s)

| NID | Function |
|---|---|
| `1F4011E6` | sceCtrlSetSamplingMode |
| `1F803938` | sceCtrlReadBufferPositive |

## `sceDisplay` - 6 function(s)

| NID | Function |
|---|---|
| `0E20F177` | sceDisplaySetMode |
| `289D82FE` | sceDisplaySetFrameBuf |
| `EEDA2E54` | sceDisplayGetFrameBuf |
| `984C27E7` | sceDisplayWaitVblankStart |
| `46F186C3` | sceDisplayWaitVblankStartCB |
| `210EAB3A` | sceDisplayGetAccumulatedHcount |

## `sceGe_user` - 11 function(s)

| NID | Function |
|---|---|
| `1F6752AD` | sceGeEdramGetSize |
| `E47E40E4` | sceGeEdramGetAddr |
| `AB49E76A` | sceGeListEnQueue |
| `1C0D95A6` | sceGeListEnQueueHead |
| `E0D68148` | sceGeListUpdateStallAddr |
| `03444EB4` | sceGeListSync |
| `B287BD61` | sceGeDrawSync |
| `B448EC0D` | sceGeBreak |
| `4C06E472` | sceGeContinue |
| `A4FC06A4` | sceGeSetCallback |
| `05DB22CE` | sceGeUnsetCallback |

## `sceMpeg` - 23 function(s)

| NID | Function |
|---|---|
| `21FF80E4` | sceMpegQueryStreamOffset |
| `611E9E11` | sceMpegQueryStreamSize |
| `682A619B` | sceMpegInit |
| `874624D6` | sceMpegFinish |
| `C132E22F` | sceMpegQueryMemSize |
| `D8C5F121` | sceMpegCreate |
| `606A4649` | sceMpegDelete |
| `42560F23` | sceMpegRegistStream |
| `591A4AA2` | sceMpegUnRegistStream |
| `A780CF7E` | sceMpegMallocAvcEsBuf |
| `CEB870B1` | sceMpegFreeAvcEsBuf |
| `F8DCB679` | sceMpegQueryAtracEsSize |
| `167AFD9E` | sceMpegInitAu |
| `FE246728` | sceMpegGetAvcAu |
| `E1CE83A7` | sceMpegGetAtracAu |
| `0E3C2E9D` | sceMpegAvcDecode |
| `740FCCD1` | sceMpegAvcDecodeStop |
| `800C44DF` | sceMpegAtracDecode |
| `D7A29F46` | sceMpegRingbufferQueryMemSize |
| `37295ED8` | sceMpegRingbufferConstruct |
| `13407F13` | sceMpegRingbufferDestruct |
| `B240A59E` | sceMpegRingbufferPut |
| `B5F6DC87` | sceMpegRingbufferAvailableSize |

## `sceRtc` - 1 function(s)

| NID | Function |
|---|---|
| `E7C27D1B` | sceRtcGetCurrentClockLocalTime |

## `sceOpenPSID` - 1 function(s)

| NID | Function |
|---|---|
| `C69BEBCE` | sceOpenPSIDGetOpenPSID |

## `sceUmdUser` - 2 function(s)

| NID | Function |
|---|---|
| `8EF08FCE` | sceUmdWaitDriveStat |
| `6B4A146C` | sceUmdGetDriveStat |

## `zlibdec` - 3 function(s)

Konami private library shipped as `USRDIR/modules/zlibdec.prx`.

| NID | Function |
|---|---|
| `461C7724` | ??? |
| `18CB51AB` | ??? |
| `216D1BF1` | ??? |

## `sound` - 2 function(s)

Konami private library shipped as `USRDIR/modules/sound.prx`.

| NID | Function |
|---|---|
| `EBDDA65C` | ??? |
| `531B15D1` | ??? |

## `kjfs` - 8 function(s)

Konami private library shipped as `USRDIR/modules/kjfs.prx`.

| NID | Function |
|---|---|
| `C78E5271` | ??? |
| `93E9297F` | ??? |
| `1ECF095D` | ??? |
| `1B969771` | ??? |
| `432F631B` | ??? |
| `2B9B31B4` | ??? |
| `813E06AD` | ??? |
| `CD04902E` | ??? |

## `InterruptManager` - 3 function(s)

| NID | Function |
|---|---|
| `CA04A2B9` | sceKernelRegisterSubIntrHandler |
| `D61E6961` | sceKernelReleaseSubIntrHandler |
| `FB8E22EC` | sceKernelEnableSubIntr |

## `IoFileMgrForUser` - 9 function(s)

| NID | Function |
|---|---|
| `42EC03AC` | sceIoWrite |
| `ACE946E8` | sceIoGetstat |
| `54F5FB11` | sceIoDevctl |
| `B2A628C1` | sceIoAssign |
| `810C4BC3` | sceIoClose |
| `109F50BC` | sceIoOpen |
| `6A638D83` | sceIoRead |
| `27EB27B8` | sceIoLseek |
| `779103A0` | sceIoRename |

## `Kernel_Library` - 2 function(s)

| NID | Function |
|---|---|
| `092968F4` | sceKernelCpuSuspendIntr |
| `5F10D406` | sceKernelCpuResumeIntr |

## `LoadExecForUser` - 2 function(s)

| NID | Function |
|---|---|
| `05572A5F` | sceKernelExitGame |
| `4AC57943` | sceKernelRegisterExitCallback |

## `ModuleMgrForUser` - 5 function(s)

| NID | Function |
|---|---|
| `B7F46618` | sceKernelLoadModuleByID |
| `50F0C1EC` | sceKernelStartModule |
| `D1FF982A` | sceKernelStopModule |
| `2E0911AA` | sceKernelUnloadModule |
| `D675EBB8` | sceKernelSelfStopUnloadModule |

## `ThreadManForUser` - 25 function(s)

| NID | Function |
|---|---|
| `446D8DE6` | sceKernelCreateThread |
| `9FA03CD3` | sceKernelDeleteThread |
| `F475845D` | sceKernelStartThread |
| `AA73C935` | sceKernelExitThread |
| `809CE29B` | sceKernelExitDeleteThread |
| `383F7BCC` | sceKernelTerminateDeleteThread |
| `2C34E053` | sceKernelReleaseWaitThread |
| `9ACE131E` | sceKernelSleepThread |
| `D59EAD2F` | sceKernelWakeupThread |
| `278C0DF5` | sceKernelWaitThreadEnd |
| `CEADEB47` | sceKernelDelayThread |
| `68DA9E36` | sceKernelDelayThreadCB |
| `D6DA4BA1` | sceKernelCreateSema |
| `28B6489C` | sceKernelDeleteSema |
| `3F53E640` | sceKernelSignalSema |
| `4E3A1105` | sceKernelWaitSema |
| `6D212BAC` | sceKernelWaitSemaCB |
| `55C20A00` | sceKernelCreateEventFlag |
| `EF9E4C70` | sceKernelDeleteEventFlag |
| `1FB15A32` | sceKernelSetEventFlag |
| `812346E4` | sceKernelClearEventFlag |
| `402FCF22` | sceKernelWaitEventFlag |
| `E81CAF8F` | sceKernelCreateCallback |
| `C07BB470` | sceKernelCreateFpl |
| `D979E9BF` | sceKernelAllocateFpl |

## `UtilsForUser` - 7 function(s)

| NID | Function |
|---|---|
| `37FB5C42` | ??? |
| `79D1C3FA` | sceKernelDcacheWritebackAll |
| `3EE30821` | sceKernelDcacheWritebackRange |
| `34B9FA9E` | sceKernelDcacheWritebackInvalidateRange |
| `91E4F6A7` | sceKernelLibcClock |
| `27CC57F0` | sceKernelLibcTime |
| `71EC4271` | sceKernelLibcGettimeofday |

## `sceSuspendForUser` - 3 function(s)

| NID | Function |
|---|---|
| `EADB1BD7` | ??? |
| `3AEE7261` | ??? |
| `090CCB3F` | ??? |

## `StdioForUser` - 3 function(s)

| NID | Function |
|---|---|
| `172D316E` | sceKernelStdin |
| `A6BAB2E9` | sceKernelStdout |
| `F78BA90A` | sceKernelStderr |

## `SysMemUserForUser` - 3 function(s)

| NID | Function |
|---|---|
| `237DBD4F` | sceKernelAllocPartitionMemory |
| `B6D61D02` | sceKernelFreePartitionMemory |
| `9D9A5BA1` | sceKernelGetBlockHeadAddr |
