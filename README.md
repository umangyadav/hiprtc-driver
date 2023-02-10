# hiprtc-driver
`hiprtc-driver` is command line utility to  compile HIP programs with hipRTC APIs. hipRTC APIs currently do not support parallel compilation. 
Client applications can spawn multiple processes of hiprtc-driver to alleviate problem of parallel compilation. 

Currently this only works for MIGraphX. Logic is hardcoded in some places.

## build instructions :
`mkdir build`

`cd build`

`CXX=/opt/rocm/llvm/bin/clang++ cmake .. `

`make`


## how to use it with MIGraphX ? 
Checkout `hiprtc_driver` branch of MIGraphX. 

Change path of `hiprtc-driver` executable here : https://github.com/ROCmSoftwarePlatform/AMDMIGraphX/blob/082115452b48a4aa768d9a704dc5103fac28decb/src/targets/gpu/compile_hip.cpp#L256

Build MIGraphX from source with `-DMIGRAPHX_USE_HIPRTC=Off`. `hiprtc-driver` follows through path of `clang++` and that is why `HIPRTC=Off` for now. 
