import sys
import subprocess
import re
import time
import os
import logging

# Check and install pyserial
try:
    import serial.tools.list_ports
except ImportError:
    python_executable = sys.executable
    subprocess.check_call([python_executable, "-m", "pip", "install", "pyserial"])
    import serial.tools.list_ports


class StateMachine:
    def __init__(self):
        # Initialize variables
        self.selected_port = None
        self.ser = None
        self.selected_file = None
        self.states = [
            self.select_port,
            self.open_serial,
            self.list_and_select_files,
            self.check_ready,
            self.handshake,
            self.connect_to_OBS38S003,
            self.erase,
            self.set_fuse,
            self.send_file,
            self.reset_mcu
        ]
        self.current_state = 0

        # Configure logging
        logging.basicConfig(
            level=logging.INFO,
            format='%(asctime)s - %(levelname)s - %(message)s',
            handlers=[
                logging.StreamHandler(),
                logging.FileHandler('flashScript.log')
            ]
        )
        self.logger = logging.getLogger(__name__)

    def run(self):
        try:
            while self.current_state < len(self.states):
                state_function = self.states[self.current_state]
                success = state_function()
                if not success:
                    self.logger.error(f"State {self.current_state} failed. Returning to handshake state.")
                    self.current_state = 3  # Set to index of the 'check_ready' state
                else:
                    self.current_state += 1

        except serial.SerialException as e:
            self.logger.error(f"Error opening or closing serial connection: {e}")
        finally:
            if self.ser and self.ser.isOpen():
                self.ser.close()
                print("Serial connection closed.")

    def select_port(self):
        available_ports = list(serial.tools.list_ports.comports())

        if not available_ports:
            self.logger.error("No COM ports available.")
            return False

        print("Available COM ports:")
        for i, port_info in enumerate(available_ports, start=1):
            print(f"{i}. {port_info}")

        while True:
            try:
                selection = int(input("Select a COM port (enter the number): "))
                if 1 <= selection <= len(available_ports):
                    self.selected_port = available_ports[selection - 1].device
                    return True
                else:
                    print("Invalid selection. Please enter a valid number.")
            except ValueError:
                self.logger.error("Invalid input. Please enter a valid number.")

    def open_serial(self):
        try:
            self.ser = serial.Serial(self.selected_port, baudrate=115200, timeout=1,
                                     bytesize=serial.EIGHTBITS, parity=serial.PARITY_NONE, stopbits=serial.STOPBITS_ONE,
                                     rtscts=False, dsrdtr=False, xonxoff=False, write_timeout=None,
                                     inter_byte_timeout=None, exclusive=None)
            self.logger.info(f"Serial connection opened on {self.selected_port}")
            return True
        except serial.SerialException as e:
            self.logger.error(f"Error opening serial connection: {e}")
            return False

    def list_and_select_files(self):
        current_directory = os.getcwd()
        hex_files = [file for file in os.listdir(current_directory) if file.lower().endswith(('.hex', '.ihx'))]

        if not hex_files:
            print("No hex or ihx files found in the current directory.")
            return False

        print("Select a file by number:")
        for i, file in enumerate(hex_files, start=1):
            print(f"{i}. {file}")

        while True:
            try:
                user_input = int(input("Enter the number of the file you want to select: "))
                if 1 <= user_input <= len(hex_files):
                    self.selected_file = os.path.join(current_directory, hex_files[user_input - 1])
                    self.logger.info(f"You selected: {self.selected_file}")
                    return True
                else:
                    print("Invalid input. Please enter a valid number.")
            except ValueError:
                self.logger.error("Invalid input. Please enter a valid number.")

    def check_ready(self):
        return self.check_if_ready(expected_data=None)

    def check_if_ready(self, timeout=10, expected_data=None):
        start_time = time.time()
        try:
            while True:
                data = self.ser.readline().decode('utf-8').strip()
                self.logger.debug(f'data = {data} and expected_data = {expected_data}')

                if data:
                    self.logger.info(data)

                if expected_data and expected_data in data:
                    return True

                if expected_data is None and not data:
                    return True

                if data == "Status: 2":
                    self.logger.error(f"Something went wrong. Process ended with {data}")
                    return False

                if data == "Can try command [signature] or [idle] then [handshake] to retry":
                    self.logger.error(f"Something went wrong. Process ended with {data}")
                    return False
                
                if time.time() - start_time > timeout:
                    self.logger.error(f"Timeout reached. No valid response within {timeout} seconds. Last response was: {data}")
                    return False

        except serial.SerialException as e:
            self.logger.error(f"Error during check_if_ready: {e}")
            return False

    def send_command(self, command, expected_response, max_retries=20, retry_interval=0.5, write_delay=0.1, resend_interval=5):
        try:
            retries = 0

            while retries < max_retries:
                if retries == 0 or retries % resend_interval == 0:
                    self.ser.write(f"{command}\r\n".encode('utf-8'))
                    time.sleep(write_delay)

                response = self.ser.readline().decode('utf-8').strip()
                self.logger.info(response)

                if response == expected_response:
                    return True

                self.logger.debug(f'send {command} expects {expected_response} but gets {response} retries:{retries}/{max_retries}')
                retries += 1
                time.sleep(retry_interval)

            return False
        except serial.SerialException as e:
            self.logger.error(f"Error during send_command: {e}")
            return False

    def handshake(self):
        try:
            if self.send_command("handshake", "handshake"):
                if self.check_if_ready(timeout=30, expected_data="Status: 0"):
                    return True
        except serial.SerialException as e:
            self.logger.error(f"Error during perform_handshake: {e}")
            return False

    def connect_to_OBS38S003(self):
        try:
            if self.check_if_ready(timeout=5, expected_data="Chip read: 0xA"):
                if self.check_if_ready(timeout=5, expected_data="Returning to idle state..."):
                    return True
        except serial.SerialException as e:
            self.logger.error(f"Error during connect_to_OBS38S003: {e}")
            return False

    def erase(self):
        try:
            if self.send_command("erase", "Erasing chip..."):
                if self.check_if_ready(timeout=10, expected_data="Chip erase successful"):
                    return True
        except Exception as e:
            self.logger.error(f"Error during erase: {e}")
            return False

    def set_fuse(self):
        try:
            if self.send_command("setfuse 18 249", "Set configuration byte..."):
                if self.check_if_ready(timeout=5, expected_data="Status: 0"):
                    if self.check_if_ready(timeout=5, expected_data="Wrote configuration byte"):
                        return True
        except Exception as e:
            self.logger.error(f"Error during set_fuse: {e}")
            return False

    def send_file(self):
        try:
            if self.check_if_ready(timeout=1):
                with open(self.selected_file, 'r') as file:
                    lines = file.readlines()
                    for i, line in enumerate(lines):
                        self.ser.write(line.encode('utf-8'))
                        response = self.ser.readline().decode('utf-8').strip()
                        self.logger.info(response)

                        if not response.strip() == line.strip():
                            self.logger.error("Error: Unexpected response or mismatch with sent line.")
                            self.logger.error(f"Sent line representation: {repr(line.strip())}")
                            self.logger.error(f"Response representation: {repr(response)}")
                            return False

                        if not i == len(lines) - 1:
                            response = self.ser.readline().decode('utf-8').strip()
                            self.logger.info(response)

                            expected = "Write successful"
                            if not response.strip() == expected:
                                self.logger.error("Error: Unexpected response or mismatch with sent line.")
                                self.logger.error(f"Sent line representation: {repr(line.strip())}")
                                self.logger.error(f"Response representation: {repr(response)}")
                                return False

                            response = self.ser.readline().decode('utf-8').strip()
                            self.logger.info(response)

                            pattern = re.compile(r'Wrote (\d+) bytes')
                            match = pattern.match(response)

                            if not match:
                                self.logger.error("Error: Unexpected response or mismatch with expected pattern.")
                                self.logger.error(f"Sent line representation: {repr(line.strip())}")
                                self.logger.error(f"Response representation: {repr(response)}")
                                return False

                    return True
        except Exception as e:
            self.logger.error(f"Error sending file content: {e}")
            return False

    def reset_mcu(self):
        try:
            if self.send_command("mcureset", "MCU reset..."):
                if self.check_if_ready(timeout=1):
                    return True
        except Exception as e:
            self.logger.error(f"Error during reset_mcu: {e}")
            return False

if __name__ == "__main__":
    state_machine = StateMachine()
    state_machine.run()