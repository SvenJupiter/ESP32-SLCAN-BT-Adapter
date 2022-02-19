from math import ceil
import sys, os, time
import argparse
import bluetooth



def get_cli_args():

    # Set up the parser
    parser = argparse.ArgumentParser()
    parser.add_argument("-f", "--firmware", type=str, help="Firmware File", default="firmware.bin")
    parser.add_argument("-d", "--device_name", type=str, help="Bluetooth Device", default="myESP32")
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



def do_firmware_update(firmware_filename, device, service):

    _, name = device
    host, port = service["host"], service["port"]

    try:
        # Create the client socket
        print(f"Connecting to \"{name}\" on {host} channel {port}")
        sock = bluetooth.BluetoothSocket(bluetooth.RFCOMM)
        sock.connect((host, port))

        try:
            print("Connected.")

            firmware_filesize = os.path.getsize(firmware_filename)
            with open(firmware_filename, "rb") as firmware:


                # Start update process
                print("Starting OTA-Update....")
                print("START BT-OTA")
                # sock.send("START BT-OTA\r\n")
                sock.send("START BT-OTA\r")

                # Initial Handshake
                msg = sock.recv(1024)
                msg = str(msg, encoding="utf8").strip()
                print(msg)
                if msg != "DO FIRMWARE UPLOAD?":
                    print("ABORT!")
                    sock.send("ABORT!\r\n")
                else:
                    print("YES")
                    sock.send("YES\r\n")

                    # Send firmware filesize
                    msg = sock.recv(1024)
                    msg = str(msg, encoding="utf8").strip()
                    print(msg)
                    if msg != "FIRMWARE FILESIZE?":
                        print("ABORT!")
                        sock.send("ABORT!\r\n")
                    else:
                        print(f"{firmware_filesize}")
                        sock.send(f"{firmware_filesize}\r\n")

                        # Get max chunk size
                        msg = sock.recv(1024)
                        msg = str(msg, encoding="utf8").strip()
                        print(msg)
                        if not msg.startswith("MAX CHUNK SIZE = "):
                            print("ABORT!")
                            sock.send("ABORT\r\n")
                        else:
                            try:
                                chunk_size = int(msg[17:])
                                print(f"Received chunk size of {chunk_size}")
                            except ValueError:
                                print("ABORT!")
                                sock.send("ABORT!\r\n")
                            else:
                                print("OK!")
                                sock.send("OK\r\n")



                                # Sync upload
                                msg = sock.recv(1024)
                                msg = str(msg, encoding="utf8").strip()
                                print(msg)
                                if msg != "START UPLOAD!":
                                    print("ABORT!")
                                    sock.send("ABORT!\r\n")
                                else:
                                    n = ceil(firmware_filesize / chunk_size)
                                    i = 1
                                    while True:
                                        chunk = firmware.read(chunk_size)
                                        if not chunk:
                                            print(f"Warning no more chunks!!!")
                                            sock.send(chunk)
                                            break
                                        else:
                                            print(f"Sending chunk {i}/{n} ({100*i/n:.2f}%) of size {len(chunk)}")
                                            sock.send(chunk)
                                            i += 1

                                        # check if upload complete or next chunk
                                        msg = sock.recv(1024)
                                        msg = str(msg, encoding="utf8").strip()
                                        print(msg)
                                        if msg == "NEXT CHUNK!":
                                            continue
                                        elif msg == "UPLOAD COMPLETE?":
                                            chunk = firmware.read(chunk_size)
                                            if not chunk:
                                                print("YES")
                                                sock.send("YES\r\n")


                                                # check OTA end
                                                msg = sock.recv(1024)
                                                msg = str(msg, encoding="utf8").strip()
                                                print(msg)
                                                if msg != "OK!":
                                                    print("ABORT!")
                                                    sock.send("ABORT\r\n")

                                                break

                                            else:
                                                print("NO!")
                                                sock.send("NO\r\n")
                                                break
                                        else:
                                            print("ABORT!")
                                            sock.send("ABORT\r\n")
                                            break


        except Exception as err:
            print(err)
            raise

        finally:
            sock.close()
            print("Connection closed")

    except bluetooth.BluetoothError as err:
        print(err)
        raise



def main():

    args = get_cli_args()
    if not args.device_address:
        device = find_device_address(args.device_name)
    else:
        device = args.device_address, args.device_name


    if not args.service_channel or args.service_channel <= 0:
        service = find_spp_service(device)
    else:
        service = {"host": args.device_address, "port": args.service_channel}


    print("Starting firmware update...")
    time.sleep(5.0)
    do_firmware_update(args.firmware, device, service)


    


if __name__ == "__main__":
    main()
