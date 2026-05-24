# Iterate optional transport adapters. Each adapter lives in transports/<name>/
# and is gated by CONDUIT_TRANSPORT_<NAME>=ON. Disabled adapters are never
# parsed, so their dependencies are never fetched.

if(CONDUIT_TRANSPORT_MQTT)
    add_subdirectory(mqtt)
endif()

if(CONDUIT_TRANSPORT_AMQP)
    add_subdirectory(amqp)
endif()

if(CONDUIT_TRANSPORT_REDIS)
    add_subdirectory(redis)
endif()

if(CONDUIT_TRANSPORT_NATS)
    add_subdirectory(nats)
endif()

if(CONDUIT_TRANSPORT_ZMQ)
    add_subdirectory(zmq)
endif()
