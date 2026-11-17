CXX ?= g++
SKETCH = ../../firmware/rttt_sensor_fusion

run: fusion_sim
	./fusion_sim

fusion_sim: fusion_sim.cpp $(SKETCH)/rttt_sensor_fusion.ino $(SKETCH)/config.h
	$(CXX) -std=gnu++17 -O1 -I mock -I $(SKETCH) fusion_sim.cpp -o $@

clean:
	rm -f fusion_sim
