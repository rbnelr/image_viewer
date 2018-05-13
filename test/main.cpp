
#include <memory>
using std::unique_ptr;
using std::make_unique;
using std::shared_ptr;
using std::make_shared;

#include "stdio.h"

class A {
public:

	A () {
		printf("A()\n");
	}
	~A () {
		printf("~A()\n");
	}
};
class B : A {
public:
	
	B () {
		printf("B()\n");
	}
	~B () {
		printf("~B()\n");
	}
};

void evict (B* ptr) {
	printf("evict()\n");
	delete ptr;
}

int main () {
	
	unique_ptr<B, void(*)(B*)> b(new B(), evict );

	shared_ptr<A> a( std::move(b) );

	return 0;
}
