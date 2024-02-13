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


# Configure the logging module
logging.basicConfig(
    level=logging.INFO,  # Set the logging level (DEBUG, INFO, WARNING, ERROR, CRITICAL)
    format='%(asctime)s - %(levelname)s - %(message)s',  # Define the log message format
    handlers=[
        logging.StreamHandler(),  # Output log messages to the console
        logging.FileHandler('flashScript.log')  # Write log messages to a file
    ]
)

# Create a logger instance
logger = logging.getLogger(__name__)

def select_com_port():
    available_ports = list(serial.tools.list_ports.comports())

    if not available_ports:
        logger.error("No COM ports available.")
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
            logger.error("Invalid input. Please enter a valid number.")


def check_if_ready(ser, timeout=10, expected_data=None):
    start_time = time.time()
    try:
        while True:
            
            # Read a line from the serial port
            data = ser.readline().decode('utf-8').strip()
            logger.debug(f'data = {data} and expected_data = {expected_data}')
            # Print the received data to the console
            if data:
                logger.info(data)
            # Check if the expected data is in the received data
            if expected_data and expected_data in data:
                return True

            # Check if there is no new line (empty string)
            if expected_data is None and not data:
                return True

            if data == "Status: 2":
                logger.error("Something went wrong. Process ended with {data}")
                return False

            # Check if the elapsed time exceeds the timeout
            if time.time() - start_time > timeout:
                logger.error(f"Timeout reached. No valid response within {timeout} seconds. Last response was: {data}")
                return False
            
        
    except serial.SerialException as e:
        print(f"Error during check_if_ready: {e}")
        return False


def send_command(ser, command, expected_response, max_retries=20, retry_interval=0.5, write_delay=0.1, resend_interval=5):
    try:
        retries = 0

        while retries < max_retries:
            if retries == 0 or retries % resend_interval == 0:
                ser.write(f"{command}\r\n".encode('utf-8'))
                time.sleep(write_delay)  # Add a delay after writing the command

            response = ser.readline().decode('utf-8').strip()
            logger.info(response)

            if response == expected_response:
                return True
            logger.debug(f'send {command} expects {expected_response} but gets {response} retries:{retries}/{max_retries}')
            retries += 1
            time.sleep(retry_interval)  # Add a delay before the next retry

        return False
    except serial.SerialException as e:
        logger.error(f"Error during send_command: {e}")
        return False



def handshake(ser):
    try:
        if send_command(ser,"handshake","handshake"):
            if check_if_ready(ser,expected_data="cycle power to target (start with power off and then turn on)"):
                if check_if_ready(ser,timeout=30,expected_data="Status: 0"):
                    return True
    except serial.SerialException as e:
        logger.error(f"Error during perform_handshake: {e}")
        return False


def connect_to_OBS38S003(ser):
    try:
        if check_if_ready(ser,timeout=5,expected_data="Chip read: 0xA"):
            if check_if_ready(ser,timeout=5,expected_data="Returning to idle state..."):
                return True
    except serial.SerialException as e:
        logger.error(f"Error during connect_to_OBS38S003: {e}")
        return False

def erase_mcu(ser):
    try:
        if send_command(ser,"erase","Erasing chip..."):
            if check_if_ready(ser,timeout=10,expected_data="Chip erase successful"):
                    return True
    except Exception as e:
        logger.error(f"Error during connect_to_OBS38S003: {e}")
        return False


def set_fuse(ser):
    try:
        if send_command(ser,"setfuse 18 249","Set configuration byte..."):
            if check_if_ready(ser,timeout=5,expected_data="Status: 0"):
                if check_if_ready(ser,timeout=5,expected_data="Wrote configuration byte"):
                    return True
    except Exception as e:
        logger.error(f"Error during connect_to_OBS38S003: {e}")
        return False


def send_file_content(ser, file_path):
    try:
        if check_if_ready(ser, timeout=1):
            with open(file_path, 'r') as file:
                lines = file.readlines()
                for i, line in enumerate(lines):
                    # Send each line to the serial port
                    ser.write(line.encode('utf-8'))
                    
                    # Wait for a response from the device
                    response = ser.readline().decode('utf-8').strip()
                    logger.info(response)
                    if not response.strip() == line.strip():
                        logger.error("Error: Unexpected response or mismatch with sent line.")
                        logger.error(f"Sent line representation: {repr(line.strip())}")
                        logger.error(f"Response representation: {repr(response)}")
                        return False
                    
                    if not i == len(lines) - 1:
                        response = ser.readline().decode('utf-8').strip()
                        logger.info(response)

                        expected = "Write successful"
                        if not response.strip() == expected:
                            logger.error("Error: Unexpected response or mismatch with sent line.")
                            logger.error(f"Sent line representation: {repr(line.strip())}")
                            logger.error(f"Response representation: {repr(response)}")
                            return False
                        
                        # Regular behavior, use regular expression to check the response pattern
                        response = ser.readline().decode('utf-8').strip()
                        logger.info(response)

                        pattern = re.compile(r'Wrote (\d+) bytes')
                        match = pattern.match(response)

                        # Check if the response matches the expected pattern
                        if not match:
                            logger.error("Error: Unexpected response or mismatch with expected pattern.")
                            logger.error(f"Sent line representation: {repr(line.strip())}")
                            logger.error(f"Response representation: {repr(response)}")
                            return False

                return True
    except Exception as e:
        print(f"Error sending file content: {e}")
        return False


def reset_mcu(ser):
    try:
        if send_command(ser,"mcureset","MCU reset..."):
            if check_if_ready(ser,timeout=1):
                return True
    except Exception as e:
        logger.error(f"Error during connect_to_OBS38S003: {e}")
        return False


class StateMachine:
    def __init__(self):
        self.selected_port = None
        self.ser = None
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

    def run(self):
        try:
            while self.current_state < len(self.states):
                state_function = self.states[self.current_state]
                success = state_function()
                if not success:
                    logger.error(f"State {self.current_state} failed. Returning to handshake state.")
                    self.current_state = 3  # Set to index of the 'check_ready' state
                else:
                    self.current_state += 1

        except serial.SerialException as e:
            logger.error(f"Error opening or closing serial connection: {e}")
        finally:
            if self.ser and self.ser.isOpen():
                self.ser.close()
                print("Serial connection closed.")

    def select_port(self):
        self.selected_port = select_com_port()
        if self.selected_port:
            logger.info(f"Selected port: {self.selected_port}")
            return True
        else:
            logger.error("No port selected.")
            return False

    def open_serial(self):
        try:
            self.ser = serial.Serial(self.selected_port, baudrate=115200, timeout=1,
                                     bytesize=serial.EIGHTBITS, parity=serial.PARITY_NONE, stopbits=serial.STOPBITS_ONE,
                                     rtscts=False, dsrdtr=False, xonxoff=False, write_timeout=None,
                                     inter_byte_timeout=None, exclusive=None)
            logger.info(f"Serial connection opened on {self.selected_port}")
            return True
        except serial.SerialException as e:
            logger.error(f"Error opening serial connection: {e}")
            return False

    def list_and_select_files(self):
        current_directory = os.getcwd()
        hex_files = [file for file in os.listdir(current_directory) if file.lower().endswith(('.hex', '.ihx'))]

        if not hex_files:
            print("No hex or ihx files found in the current directory.")
            return

        print("Select a file by number:")
        for i, file in enumerate(hex_files, start=1):
            print(f"{i}. {file}")

        while True:
            try:
                user_input = int(input("Enter the number of the file you want to select: "))
                if 1 <= user_input <= len(hex_files):
                    self.selected_file = os.path.join(current_directory, hex_files[user_input - 1])
                    logger.info(f"You selected: {self.selected_file}")
                    return
                else:
                    print("Invalid input. Please enter a valid number.")
            except ValueError:
                logger.error("Invalid input. Please enter a valid number.")
    
    def check_ready(self):
        return check_if_ready(self.ser)

    def handshake(self):
        return handshake(self.ser)

    def connect_to_OBS38S003(self):
        return connect_to_OBS38S003(self.ser)

    def erase(self):
        return erase_mcu(self.ser)

    def set_fuse(self):
        return set_fuse(self.ser)

    def send_file(self):
        return send_file_content(self.ser, self.selected_file)

    def reset_mcu(self):
        return reset_mcu(self.ser)

if __name__ == "__main__":
    state_machine = StateMachine()
    state_machine.run()

