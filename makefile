server: main.cpp reactor.cpp httpdeal.cpp ./log/log.cpp ./sql/connection_pool.cpp
		$(CXX) -o server  $^ $(CXXFLAGS) -lpthread -lmysqlclient

clean:
		rm -r server