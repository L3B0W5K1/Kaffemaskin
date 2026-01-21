
server start:
simon@simon-UP-APL01:~/leshan/leshan-demo-server$ java -jar target/*with-dependencies.jar



test device:
simon@simon-UP-APL01:~/leshan/leshan-demo-client$ java -jar target/leshan-demo-client-*-jar-with-dependencies.jar -n Device1






wakaama:

setup:

simon@simon-UP-APL01 :~ /wakaama/examples/client/udp$ cmake -S . -B build-client-udp
simon@simon-UP-APL01 :~ /wakaama/examples/client/udp$ cmake --build build-client-udp

start:
simon@simon-UP-APL01:~/wakaama/examples/client/udp$ ./build-client-udp/lwm2mclient -n wakaama-client-1 -h 127.0.0.1 -p 5683 -4


