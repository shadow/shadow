set(core_srcs
	events.c
	evtracker.c
	log.c
	shd-main.c
	process_controller.c
	process.c
	routing.c
	sim_master.c
	sim_slave.c
	sim_worker.c
	simnet_graph.c
	simop.c
	sysconfig.c
	timer.c
)

set(node_srcs
	node/context.c
    node/module.c 
    node/snricall.c 
)

set(vevent_srcs
	vevent/vepoll.c
	vevent/vevent.c
	vevent/vevent_mgr.c
)

set(vnetwork_srcs
	vnetwork/vbuffer.c
	vnetwork/vci.c
	vnetwork/vpacket_mgr.c
	vnetwork/vpacket.c
	vnetwork/vpipe.c
    vnetwork/vpeer.c
    vnetwork/vsocket_mgr.c
	vnetwork/vsocket.c
	vnetwork/vtcp_server.c
	vnetwork/vtcp.c
	vnetwork/vtransport_mgr.c
	vnetwork/vtransport.c
	vnetwork/vudp.c
)

set(vsystem_srcs
	vsystem/resolver.c
	vsystem/vcpu.c
	vsystem/vsystem.c
)

# dvn executable
add_executable(shadow-bin ${core_srcs} ${node_srcs} ${vevent_srcs} ${vsystem_srcs} ${vnetwork_srcs})
add_dependencies(shadow-bin shadow-intercept shadow-util shadow-dsim)
target_link_libraries(shadow-bin shadow-intercept shadow-util shadow-dsim ${M_LIBRARIES} ${DL_LIBRARIES} ${RT_LIBRARIES})
install(TARGETS shadow-bin DESTINATION bin)

add_executable(shadow-controller shd-controller-main.c)
add_dependencies(shadow-controller shadow-util)
target_link_libraries(shadow-controller shadow-util)

