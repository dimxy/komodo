---
![Komodo Logo](https://i.imgur.com/E8LtkAa.png "Komodo Logo")

See the original komodo readme [here](https://github.com/komodoplatform/komodo)

## Build with websockets support

This is an experimental komodod build with websockets support which allows to connect nSPV clients over websockets protocol to nSPV komodod daemon.<br>
To build use the orginal build command files (./zcutil/build.sh, build-mac.sh, build-win.sh) with a parameter `--enable-websockets`.
```
./zcutil/build.sh --enable-websockets
```
It is recommended to start with a fresh clone repo or perform 'make clean' before building.<br>
After the komodod is built it might be started with a couple of new parameters:<br>
`-wsport=<port>` - websockets listening port (default is 8192)<br>
`addwsnode=<ip>:<port>` - other websocket node address<br> 

The experimental nSPV javascript client with a cc faucet sample is here: https://github.com/dimxy/bitgo-komodo-cc-lib

---


Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
