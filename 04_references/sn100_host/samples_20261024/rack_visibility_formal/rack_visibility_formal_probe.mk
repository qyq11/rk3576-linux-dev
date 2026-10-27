include Makefile

../visibility_formal/rack_visibility_probe.o: ../rack_visibility_probe.cpp
	$(CXX) -c $(CXXFLAGS) $(INCPATH) -o $@ $<

../visibility_formal/rack_visibility_probe: ../visibility_formal/rack_visibility_probe.o .obj/sample_page.o
	$(LINK) $(LFLAGS) -o $@ ../visibility_formal/rack_visibility_probe.o $(filter-out .obj/samples_test.o,$(OBJECTS)) $(LIBS)
