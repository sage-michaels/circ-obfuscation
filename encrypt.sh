#!/usr/bin/env bash

#this script is for a user looking to encrypt a message
#it expects an input in binary with spaces between each input a circuit and the starting slot position
#example: ./encryptnum 1 1 0 0 circuits/comp2.dsl.acirc 0 {binary number is 1100 and it starts at slot 0}


last=$#;
startSlot=${!last};
i=1;
circuitLoc=$(($last - 1));
circuit=${!circuitLoc};
endOfWhile=$(($circuitLoc));
echo last: $last circuitLoc: $circuitLoc circuit: $circuit
while [[ $i -lt $endOfWhile ]];
do
  echo "encrypting ${!i}";
  ./mio mife encrypt --mmap CLT $circuit ${!i} $startSlot;
  let startSlot+=1;
  let i+=1;
done;

  





