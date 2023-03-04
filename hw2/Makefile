all: hw2

hw2: hw2.o hw2_output.o
	g++ -pthread -o '$@' $^ 


hw2_output.o: hw2_output.c
	gcc -c -o '$@' '$<'


hw2.o: hw2.cpp
	g++ -c -o '$@' '$<'


clean:
	rm -f *.o *.out
