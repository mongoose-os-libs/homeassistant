# Home Assistant for Mongoose OS

This library implements a MQTT aware [Home Assistant](https://home-assistant.io/)
interface for Mongoose OS. It offers a low-level API which allows users to
add entities with _command_, _status_ and _attributes_ callbacks. For more
out-of-the-box functionality, it adds a higher level API which uses a JSON
configuration file to enable lots of things: `GPIO`, `I2C`, `SPI` sensors to
name but a few.

All types of object are configured automatically in Home Assistant, with zero
configuration!

## API

### Low Level API

This API allows the creation of a _node_, adding _objects_ to that node, and
_classes_ to the created objects. It then allows objects to send their _status_
and receive _command_ and _attribute_ callbacks via MQTT.

#### Node API

Upon library initialization, a global `mgos_homeassistant` object is created.
It can be returned using ***`mgos_homeassistant_get_global()`***. After adding
_object_ and optionally _class_ entries, (see below), _config_ and _status_ can
be sent for all objects using ***`mgos_homeassistant_send_config()`*** and
***`mgos_homeassistant_send_status()`*** respectively.

To recursively remove all objects and their associated classes, call
***`mgos_homeassistant_clear()`***. A higher level configuration based
construction of objects and classes is described below.

#### Object API

*   ***`mgos_homeassistant_object_add()`*** creates a new _object_ with the
    given name and of type. If additional JSON configuration payload is needed,
    a pointer to it can be provided or NULL passed. A callback for _status_
    calls is provided, and optionally a _userdata_ pointer is passed.
*   ***`mgos_homeassistant_object_search()`*** searches the structure for an
    object with the given name. It returns a pointer to the object or NULL
    if none are found.
*   ***`mgos_homeassistant_object_get_userdata()`*** returns the provided
    _userdata_ struct upon creation.
*   ***`mgos_homeassistant_object_set_cmd_cb()`*** sets the callback function
    for _command_ MQTT requests.
*   ***`mgos_homeassistant_object_set_attr_cb()`*** sets the callback function
    for _attribute_ MQTT requests.
*   ***`mgos_homeassistant_object_send_status()`*** sends an MQTT update with
    the _status_ of the object. The status itself is provided by the callback
    given at object creation time.
*   ***`mgos_homeassistant_object_send_config()`*** sends a MQTT update with
    the configuration of the object (and its children, see below).
*   ***`mgos_homeassistant_object_remove()`*** removes the object (and its
    children, see below) from `ha` structure.

#### Class API

After creation of an _object_, multiple classes can be added that share one
status update. For each _class_ added, a unique _classname_ must be provided.
Then, when _status_ is sent, each class will be called in turn and their
status callback results added to the status of the parent object. This allows
for sensors and things with multiple components (like a barometer containing
a pressure, thermometer and hygrometer sensor all in one package) to generate
three _config_ lines with one _status_ line.

*   ***`mgos_homeassistant_object_class_add()`*** creates a new class under
    the provided object. The _classname_ must be unique. If additional JSON
    configuration payload is needed, it can be optionally passed. A callback
    for _status_ is provided, and will be appended to the object's _status_
    calls.
*   ***`mgos_homeassistant_object_class_send_status()`*** causes the class
    to request its parent object to send _status_, including this and all
    sibling classes.
*   ***`mgos_homeassistant_object_class_send_config()`*** causes the class
    to send its own _config_.
*   ***`mgos_homeassistant_object_class_remove()`*** removes the class from
    its parent object.

### High Level API 

(TODO)

## MQTT Discovery

The library fully implements Home Assistant's [MQTT Discovery](https://home-assistant.io/docs/mqtt/discovery/)
protocol, which dictates discovery topics as:

`<discovery_prefix>/<component>/[<node_id>/]<object_id>/config`

Here, we map the fields to objects as follows:

*    `<discovery_prefix>` is `mos.yml`'s `homeassistant.discovery_prefix`
     (eg. `homeassistant`)
*    `<node_id>` (mandatory) is `mos.yml`'s `device_id` (eg. `esp8266_C45ADA`).
*    `<component>` (mandatory) is one of the Home Assistant discoverable types,
     like `binary_sensor` or `switch`.
*    `<class_id>` (optional) is the `<device_class>`, an attribute specific to
     the `component` (eg. class `motion` for component `binary_sensor`).
*    `<provider>` (mandatory) is the Mongoose OS driver that is implementing
     the object (eg. `gpio`,`si7021` or `barometer`).
*    `<index>` (mandatory) is a number that is used in case multiple instances
     on the provider exist (eg. `0`, `2`, `12` for `gpio`).

Derived from these are:

*    `<object_id>` is a composed string that _uniquely_ identifies the
     component on the node: `<provider>_<index>[_<class_id>]`.
*    `<name>` is a composed string that _uniquely_ identifies the object in
     Home Assistant: `<node_id>_<object_id>`. It can also be set explicitly
     when creating the object.

Examples of a discovery config topic:
```
homeassistant/switch/esp8266_C45ADA/LED/config
homeassistant/binary_sensor/esp8266_C45ADA/button/config
homeassistant/sensor/esp8266_C45ADA/barometer_0_temperature/config
homeassistant/sensor/esp8266_C45ADA/barometer_0_pressure/config
homeassistant/sensor/esp8266_C45ADA/barometer_0_humidity/config
```
The first two examples above do not have a `device_class` setting.
The third through fifth ones have a `device_class` of `temperature`, `pressure`
and `humidity` respectively. This is because in Home Assitant, each entity has
to be configured by its own unique discovery topic.

It's worth noting that while discovery topics have to be unique, the state
topics do not, and often are shared between all `device_class` on the same
`object_id`, for example the `barometer` may (and does) combine state updates
of all three sensor readings (humidity, temperature and pressure) in one topic
update. Providers that want to combine status updates should initialize the
objects by setting their `topic_prefix_use_class` to `false`.

The `<name>` of an object is what HA will use to key `<entity_id>` off of, and
it is derived from the keys above. In order to create stable, unique, and
predictable IDs, the `<node_id>` will have to be a part of the `<name>`, too
(see above).

As a result of this decision the `<name>` will be a 1:1 mapping to the 
`<entity_id>` in Home Assistant. The resulting entities for the config topics
described above are thus:

```
switch.esp8266_C45ADA_gpio_12
binary_sensor.esp8266_C45ADA_gpio_0
sensor.esp8266_C45ADA_barometer_0_temperature
sensor.esp8266_C45ADA_barometer_0_pressure
sensor.esp8266_C45ADA_si7021_0_humidity
```

### MQTT Topics

For each object, a `<topic_prefix>` is derived:
*    `<topic_prefix>` is `<node_id>/<component>/<name>[_<class>/]<index>`.
*    `<topics>` are then appended, eg. `/stat` or `/cmd`.

Implementations are able to report all data on an object either in multiple
messages, one-per-class, or in single JSON messages, one-forall-classes in the
object. The latter is preferred. The payloads thus, are either literals or JSON
messages.

Examples of MQTT topics and payloads:
```
esp8266_C45ADA/binary_sensor/pir0 {"motion":false}
esp8266_24538D/sensor/button/stat {"action":"click","count":2}
esp8266_C45ADA/switch/LED {"state":"ON"}
esp8266_C45ADA/sensor/si7021_0 {"temperature":17.58,"humidity":45.5}
esp8266_C45ADA/sensor/barometer_0 {"pressure":974.40,"temperature":17.15}

```

The reason for using a tree hierarchy with `/` delimiters here is to enable
nodes to subscribe to relevant topics like `esp8266_C45ADA/switch/#` to receive
and process commands from Home Assistant.

Each object will listen on the `<topic_prefix>/#` wildcard, and install one or
more handlers:
*   `/stat` -- always installed. Sending an empty message to this topic will
    make the device send a _status_ update for the object.
*   `/cmd` -- for some object types, notably _switch_ this topic will accept
    commands that change the state (for example, setting an LED or Relay on
    or off).
*   `/attr` -- for those objects that implement it, additional JSON attributes
    for the object can be queried by sending an empty message to this topic.

## Supported Drivers

TODO(pim).
