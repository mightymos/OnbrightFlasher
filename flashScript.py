import sys
import subprocess
import re

# Check and install pyserial
try:
    import serial.tools.list_ports
except ImportError:
    python_executable = sys.executable
    subprocess.check_call([python_executable, "-m", "pip", "install", "pyserial"])
    import serial.tools.list_ports


def select_com_port():
    available_ports = list(serial.tools.list_ports.comports())

    if not available_ports:
        print("No COM ports available.")
        return None

    print("Available COM ports:")
    for i, port_info in enumerate(available_ports, start=1):
        print(f"{i}. {port_info}")

    while True:
        try:
            selection = int(input("Select a COM port (enter the number): "))
            if 1 <= selection <= len(available_ports):
                selected_port = available_ports[selection - 1].device
                return selected_port
            else:
                print("Invalid selection. Please enter a valid number.")
        except ValueError:
            print("Invalid input. Please enter a valid number.")


def perform_handshake_ESP8285(ser):
    try:
        # Send handshake message
        ser.write(b"handshake\r\n")

        # Read and print the response
        response_line1 = ser.readline().decode('utf-8').strip()
        response_line2 = ser.readline().decode('utf-8').strip()
        response = f"{response_line1}\n{response_line2}"
        print(f"Received response:\n{response}")

        if response_line1.lower() == "handshake":
            print("Handshake successful!")
            return True
        elif re.match(r"Handshake failed \(\d+\)", response_line1):
            print("Handshake failed.")
            return False
        else:
            print("Unexpected response format.")
            return False
    except serial.SerialException as e:
        print(f"Error during handshake: {e}")
        return False


def perform_handshake_OBS38S003(ser):
    try:
        # Wait for the "Handshake complete..." response
        response = ser.readline().decode('utf-8').strip()
        while "Handshake complete" not in response:
            print(response)
            response = ser.readline().decode('utf-8').strip()

        print(response)

        response = ser.readline().decode('utf-8').strip()
        print(response)
        # Check for different scenarios after "Handshake complete"
        if "Write address failed" in response:
            # Wait for the "However, chip type reported was: 0xFF" response
            response = ser.readline().decode('utf-8').strip()
            while "However, chip type reported was: 0xFF" not in response:
                print(response)
                response = ser.readline().decode('utf-8').strip()

            print(response)
            # Wait for the "Can try command [signature] or [handshake] to retry" response
            response = ser.readline().decode('utf-8').strip()
            while "Can try command [signature] or [handshake] to retry" not in response:
                print(response)
                response = ser.readline().decode('utf-8').strip()

            print(response)            

            if erase_mcu(ser):
                return set_fuse(ser)

            return False

        elif "Chip read: 0xA" in response:
            # Wait for the "Connected..." response
            response = ser.readline().decode('utf-8').strip()
            while "Connected" not in response:
                print(response)
                response = ser.readline().decode('utf-8').strip()

            print(response)

            # Wait for the "Returning to idle state..." response
            response = ser.readline().decode('utf-8').strip()
            while "Returning to idle state" not in response:
                print(response)
                response = ser.readline().decode('utf-8').strip()

            print(response)

            print("Initialization complete!")

            return True
        else:
            print("Unexpected response after Handshake complete.")
            return False
    except Exception as e:
        print(f"Error during handshake: {e}")
        return False



def reset_mcu(ser):
    try:
        # Send MCU reset command
        ser.write(b"mcureset\r\n")

        # Wait for the response
        response = ser.readline().decode('utf-8').strip()
        print(f"Response: {response}")

        # Check if the response matches the expected pattern
        if response.lower() == "mcureset":
            print("MCU reset successful.")
            return True
        else:
            print("Error: Unexpected response or MCU reset failed.")
            print(f"Expected response: mcureset")
            print(f"Actual response: {response}")
            return False

    except Exception as e:
        print(f"Error during MCU reset: {e}")
        return False


def send_file_content(ser, file_path):
    try:
        with open(file_path, 'r') as file:
            lines = file.readlines()
            for i, line in enumerate(lines):
                # Send each line to the serial port
                ser.write(line.encode('utf-8'))
                
                # Wait for a response from the device
                response = ser.readline().decode('utf-8').strip()
                print(f"Response: {response}")

                # Check if the response contains the expected pattern
                if response.strip() == line.strip():
                    print(f"File content sent successfully.")
                else:
                    print("Error: Unexpected response or mismatch with sent line.")
                    print(f"Sent line representation: {repr(line.strip())}")
                    print(f"Response representation: {repr(response)}")
                    return False

                if not i == len(lines) - 1:
                    # Regular behavior, use regular expression to check the response pattern
                    response = ser.readline().decode('utf-8').strip()
                    print(f"Response: {response}")

                    pattern = re.compile(r'Wrote (\d+) bytes')
                    match = pattern.match(response)

                    # Check if the response matches the expected pattern
                    if match:
                        print(f"File content sent successfully.")
                    else:
                        print("Error: Unexpected response or mismatch with expected pattern.")
                        print(f"Sent line representation: {repr(line.strip())}")
                        print(f"Response representation: {repr(response)}")
                        return False

            return True
    except Exception as e:
        print(f"Error sending file content: {e}")
        return False


def erase_mcu(ser):
    try:
        # Send MCU erase command
        ser.write(b"mcuerase\r\n")

        # Wait for the first response
        response1 = ser.readline().decode('utf-8').strip()
        print(f"Response 1: {response1}")

        # Check if the first response matches the expected pattern
        if response1.lower() == "mcuerase":
            # Wait for the second response
            response2 = ser.readline().decode('utf-8').strip()
            print(f"Response 2: {response2}")

            # Check if the second response matches the expected pattern
            if response2.lower() == "chip erase successful":
                print("MCU erase successful.")
                return True
            else:
                print("Error: Unexpected second response or chip erase failed.")
                return False
        else:
            print("Error: Unexpected first response or MCU erase failed.")
            return False

    except Exception as e:
        print(f"Error during MCU erase: {e}")
        return False


def set_fuse(ser):
    try:
        # Send set fuse command
        ser.write(b"setfuse 18 249\r\n")

        # Wait for the first response
        response1 = ser.readline().decode('utf-8').strip()
        print(f"Response 1: {response1}")

        # Check if the first response matches the expected pattern
        if response1.lower() == "setfuse 18 249":
            # Wait for the second response
            response2 = ser.readline().decode('utf-8').strip()
            print(f"Response 2: {response2}")

            # Check if the second response matches the expected pattern
            if response2.lower() == "set configuration byte":
                # Wait for the third response
                response3 = ser.readline().decode('utf-8').strip()
                print(f"Response 3: {response3}")

                # Check if the third response matches the expected pattern
                if response3.lower() == "wrote configuration byte":
                    print("Fuse configuration successful.")
                    return True
                else:
                    print("Error: Unexpected third response.")
                    return False
            else:
                print("Error: Unexpected second response.")
                return False
        else:
            print("Error: Unexpected first response.")
            return False

    except Exception as e:
        print(f"Error during fuse configuration: {e}")
        return False



def main():
    selected_port = select_com_port()

    if selected_port:
        try:
            ser = serial.Serial(selected_port, baudrate=115200, timeout=1, 
                                bytesize=serial.EIGHTBITS, parity=serial.PARITY_NONE, stopbits=serial.STOPBITS_ONE,
                                rtscts=False, dsrdtr=False, xonxoff=False, write_timeout=None, inter_byte_timeout=None,
                                exclusive=None)
            print(f"Serial connection opened on {selected_port}")

            # Perform handshake
            if perform_handshake_ESP8285(ser):
                if perform_handshake_OBS38S003(ser):
                    if send_file_content(ser, 'blink.ihx'):
                        reset_mcu(ser)


            ser.close()
            print("Serial connection closed.")
        except serial.SerialException as e:
            print(f"Error opening or closing serial connection: {e}")

if __name__ == "__main__":
    main()