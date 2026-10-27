include Makefile

../visibility_candidate/sample_page.o: ../visibility_shadow/sample_page.cpp
	$(CXX) -c $(CXXFLAGS) $(INCPATH) -o $@ $<

../visibility_candidate/rack_visibility_probe.o: ../rack_visibility_probe.cpp
	$(CXX) -c $(CXXFLAGS) $(INCPATH) -o $@ $<

../visibility_candidate/rack_visibility_probe: ../visibility_candidate/sample_page.o ../visibility_candidate/rack_visibility_probe.o
	$(LINK) $(LFLAGS) -o $@ ../visibility_candidate/sample_page.o ../visibility_candidate/rack_visibility_probe.o $(filter-out .obj/sample_page.o .obj/samples_test.o,$(OBJECTS)) $(LIBS)
