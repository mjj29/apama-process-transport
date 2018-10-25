# apama-process-transport
Apama connectivity transport to execute a process and return it's stdout line-by-line as events

## Supported Apama version

This works with Apama 10.3.0.1 or later

## Building the plugin

In an Apama command prompt on Linux run:

    mkdir -p $APAMA_WORK/lib
	 g++ -std=c++11 -o $APAMA_WORK/lib/libProcessTransport.so -I$APAMA_HOME/include -L$APAMA_HOME/lib -lapclient -shared  -fPIC ProcessTransport.cpp
	 
## Running tests

To run the tests for the plugin you will need to use an Apama command prompt to run the tests from within the tests directory:

    pysys run


