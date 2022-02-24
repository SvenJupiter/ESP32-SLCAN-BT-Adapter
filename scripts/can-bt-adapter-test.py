import sys, argparse
import bluetooth
import can


def get_cli_args():

    # Set up the parser
    parser = argparse.ArgumentParser()

    # SLCAN arguments
    parser.add_argument("-i", "--do_init", help="Initilize CAN-BT-Adapter", action="store_true")
    parser.add_argument("-sc", "--slcan_channel", type=str, help="SLCAN Channel", default="/dev/rfcomm0")
    parser.add_argument("-uc", "--usb_channel", type=str, help="USB Channel", default="can0")
    
    # Bluetooth arguments
    parser.add_argument("-d", "--device_name", type=str, help="Bluetooth Device", default="SLCAN-BT-Adapter")
    parser.add_argument("-a", "--device_address", type=str, help="Device Address", default=None)
    parser.add_argument("-c", "--service_channel", type=int, help="Service Channel", default=None)


    # parse the arguments (uses sys.argv by default)
    args = parser.parse_args()
    print(args)
    return args

def find_device_address(device_name):

    print(f'Searching for device "{device_name}"....')
    nearby_devices = bluetooth.discover_devices(duration=8, lookup_names=True, flush_cache=True, lookup_class=False)

    try:
        # check if device was found
        idx = [x[1] for x in nearby_devices].index(device_name)
        addr, name = nearby_devices[idx]
        print(f'Device "{device_name}" was found at {addr}')
        return addr, name

    except ValueError:
        num_devices_found = len(nearby_devices)
        print(f'Device "{device_name}" was not found')
        print(f"Found {num_devices_found} devices")

        if num_devices_found <= 0:
            print("Aborting...")
            sys.exit()
        else:
            # Print a list of all found devices
            for i, device in enumerate(nearby_devices):
                addr, name = device
                try:
                    print(f"{i+1}.   {addr} - {name}")
                except UnicodeEncodeError:
                    print(f"{i+1}.   {addr} - {name.encode('utf-8', 'replace')}")
            
            while True:
                try:
                    choice = input("Choose a device: ")
                    choice = int(choice)
                    if choice == 0:
                        print("Aborting...")
                        sys.exit()
                    elif choice > 0 and choice <= num_devices_found:
                        addr, name = nearby_devices[choice-1]
                        print(f'Device {addr} - {name} selected')
                        return addr, name
                    else:
                        print("Invalid choice")

                except ValueError:
                    print("Aborting...")
                    sys.exit()

def find_spp_service(device):

    # search for SPP service
    addr, _ = device
    service_matches = bluetooth.find_service(name=None, uuid="1101", address=addr) 

    if len(service_matches) == 0:
        print(f'Couldn\'t find a SSP service.')
        sys.exit()
    else:
        print(f'Found {len(service_matches)} SSP service{"s" if len(service_matches) > 1 else ""}.')
        for i, svc in enumerate(service_matches):
            # svc = service_matches[0] # First match
            print(f"{i+1}. Service Name:", svc["name"])
            print("\t", "Host:       ", svc["host"])
            print("\t", "Description:", svc["description"])
            print("\t", "Provided By:", svc["provider"])
            print("\t", "Protocol:   ", svc["protocol"])
            print("\t", "channel/PSM:", svc["port"])
            print("\t", "svc classes:", svc["service-classes"])
            print("\t", "profiles:   ", svc["profiles"])
            print("\t", "service id: ", svc["service-id"])

        while True:
            try:
                choice = input("Choose a service: ")
                choice = int(choice)
                if choice == 0:
                    print("Aborting...")
                    sys.exit()
                elif choice > 0 and choice <= len(service_matches):
                    svc = service_matches[choice-1]
                    print(f'Service {svc["name"]} selected')
                    return svc
                else:
                    print("Invalid choice")

            except ValueError:
                print("Aborting...")
                sys.exit()




def init_slcan_adapter(device, service):

    _, name = device
    host, port = service["host"], service["port"]
    print(f"Connecting to \"{name}\" on {host} channel {port}")

    # Create the client socket
    sock = bluetooth.BluetoothSocket(bluetooth.RFCOMM)

    try:
        sock.connect((host, port))
        print(f"Connected. Initilizing...")

        try:
            # cmd = "N" # Serial number
            # cmd = "V" # Version
            # cmd = "S6" # 500 kbit/s
            # cmd = "F" # Status flags
            # cmd = "W1" # Singel mode filter
            # cmd = "Z1" # timestamps
            # cmd = "X1" # auto poll
            # cmd = "C" # Close
            # cmd = "O" # Open
            # cmd = "Q1" # auto startup
            # cmd = "P" # poll
            # cmd = "A" # poll all
            # cmd = "t123400112233"
            init_commands = ("C", "S6", "M00000000", "mFFFFFFFF", "W1", "Z0", "X1", "O", "Q1")

            for cmd in init_commands:

                cmd = bytes(cmd + "\r", encoding='utf8')
                print(f"Sending cmd {cmd} ...")
                sock.send(cmd)
                msg = sock.recv(1024)
                print(msg)

        except Exception as err:
            print(err)
            raise
        finally:
            sock.close()
            print("Connection closed")
            
    except Exception as err:
        print(err)
        raise

def do_sending_and_receiving_test(can_bt_bus, can_usb_bus, use_extended_id=False, do_remote_frame=False):

        # Sending a test request
        test_send_request = can.Message(
            arbitration_id=0x123, 
            is_extended_id=use_extended_id, 
            is_remote_frame=do_remote_frame, 
            data=b"Hello" if not do_remote_frame else b'',
            dlc=5
        )
        can_bt_bus.send(test_send_request)

        # Receiving the test request
        test_received_request = can_usb_bus.recv()

        # Checking 
        assert(test_send_request.arbitration_id == test_received_request.arbitration_id)
        assert(test_send_request.is_extended_id == test_received_request.is_extended_id)
        assert(test_send_request.is_remote_frame == test_received_request.is_remote_frame)
        assert(test_send_request.data == test_received_request.data)
        assert(test_send_request.dlc == test_received_request.dlc)

        if use_extended_id and not do_remote_frame:
            print("Extended ID Sending test successfull")
        elif do_remote_frame and not use_extended_id:
            print("Remote Frame Sending test successfull")
        elif use_extended_id and do_remote_frame:
            print("Extended ID Remote Frame Sending test successfull")
        else:
            print("Sending test successfull")

        # Sending the test response
        test_send_response = can.Message(
            arbitration_id=0x321, 
            is_extended_id=use_extended_id, 
            is_remote_frame=do_remote_frame, 
            data=b"World" if not do_remote_frame else b'',
            dlc=5
        )
        can_usb_bus.send(test_send_response)

        # Receiving the test request
        test_received_response = can_bt_bus.recv()

        # Checking 
        assert(test_send_response.arbitration_id == test_received_response.arbitration_id)
        assert(test_send_response.is_extended_id == test_received_response.is_extended_id)
        assert(test_send_response.is_remote_frame == test_received_response.is_remote_frame)
        assert(test_send_response.data == test_received_response.data)
        assert(test_send_response.dlc == test_received_response.dlc)

        if use_extended_id and not do_remote_frame:
            print("Extended ID Reveiving test successfull")
        elif do_remote_frame and not use_extended_id:
            print("Remote Frame Reveiving test successfull")
        elif use_extended_id and do_remote_frame:
            print("Extended ID Remote Frame Reveiving test successfull")
        else:
            print("Reveiving test successfull")

def do_can_bus_test(slcan_channel, usb_channel):

    try:
        print("Testing BT-SLCAN adapter")

        # Opening CAN-BT interface
        can_bt_bus = can.interface.Bus(bustype="slcan", channel=slcan_channel, ttyBaudrate=115200, bitrate=500000)

        # Opening CAN-USB interface
        can_usb_bus = can.interface.Bus(bustype="socketcan", channel=usb_channel, bitrate=500000)


        do_sending_and_receiving_test(can_bt_bus, can_usb_bus, False, False)
        do_sending_and_receiving_test(can_bt_bus, can_usb_bus, True, False)
        do_sending_and_receiving_test(can_bt_bus, can_usb_bus, False, True)
        do_sending_and_receiving_test(can_bt_bus, can_usb_bus, True, True)


    except AssertionError as err:
        print(err)

    finally:
        # Shutdown CAN-BT interface
        if can_bt_bus is not None:
            print("Shutting down SLCAN")
            can_bt_bus.shutdown()

        # Shutdown CAN-USB interface
        if can_usb_bus is not None:
            print("Shuting down USB")
            can_usb_bus.shutdown()



def main():

    # Parse cli arguments
    args = get_cli_args()

    if args.do_init:

        # Search for bluetooth device and service
        if not args.device_address:
            device = find_device_address(args.device_name)
        else:
            device = args.device_address, args.device_name

        if not args.service_channel or args.service_channel <= 0:
            service = find_spp_service(device)
        else:
            service = {"host": args.device_address, "port": args.service_channel}


        # init the slcan adapter
        init_slcan_adapter(device, service)

        print(f"Initilized {device}")
        sys.exit()

    else:
        do_can_bus_test(args.slcan_channel, args.usb_channel)


if __name__ == "__main__":
    main()


