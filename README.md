# modbus-demo-gateway
A simple demo of a modbus gateway that forwards Nabto request/responses to a modbus based serial device

```
# Fetch the stub source - note the --recursive option to also install the uNabto SDK
git clone --recursive https://github.com/nabto/modbus-demo-gateway

# got to the main directory
cd modbus-demo-gateway

./build.sh


# Run the demo with a device ID and key obtained from www.appmyproduct.com 
./amp_device_stub -d [PUT DEVICE ID FROM appmyproduct.com HERE] -k [PUT DEVICE KEY FROM appmyproduct.com HERE]
```


