# d3d_dumper

A simple tool for finding models in d3d applications.

#### General

this tool hooks dx11's drawing functions (```DrawIndexed```, ```DrawIndexedInstanced``` and ```DrawIndexedInstancedIndirect```) and parses its information



#### How to use

1. clone with ```recursive``` as this project contains submodules
2. change defines in ```kiero.h``` to use dx11 and minhook
3. compile and inject.
4. Use F1-F10 to control strides, bytewidth, etc in game