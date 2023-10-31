# Threaded Serial Monitor.py
#
# Original script modified from Super Simple Serial Monitor written in Python
# https://gist.github.com/maxwiese/db51e4a60d0c09727413e7f5f45af03f
# I (Jeremy Gerdes) had a lot of help on this script from: 
# bard.google.com and chat.gpt 3.5 in October 2023
# to make it threaded and process all com ports, and accept arguments.
import os
import argparse
import serial
import serial.tools.list_ports
import sys
from threading import Thread
from time import sleep

# Function to search for available serial ports
def search_for_ports():
    ports = list(serial.tools.list_ports.comports())
    return ports

# Function to print the names of available serial ports
def print_ports(ports):
    for port in ports:
        print("Found Serial Port: " + port.device)

# Function to write data to a file
def write_to_file(data, filename):
    try:
        with open(filename, "ab") as f:
            f.write(data)
    except Exception as e:
        print(f"Error writing to {filename}: {e}")

# Function to create a folder if it doesn't exist
def create_folder_if_missing(folder_name):
    try:
        if not os.path.exists(folder_name):
            os.makedirs(folder_name)
    except Exception as e:
        print(f"Error creating folder {folder_name}: {e}")

class SerialMonitor(Thread):
    def __init__(self, port, baudrate, filename, pipe_to_stdout):
        super().__init__()
        self.port = port
        self.baudrate = baudrate
        self.filename = filename
        self.pipe_to_stdout = pipe_to_stdout

    def run(self):
        try:
            serial_conn = serial.Serial(self.port, self.baudrate)
            with open(self.filename, "ab") as f:
                while True:
                    data = serial_conn.readline()
                    if not data:
                        break
                    # Write data to the file
                    f.write(data)
                    # Print data to stdout
                    if self.pipe_to_stdout:
                        sys.stdout.buffer.write(data)
                        sys.stdout.flush()
        except Exception as e:
            print(f"Error monitoring {self.port}: {e}")

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("-b", "--baudrate", type=int, default=1000000, help="Enter baudrate")
    parser.add_argument("-f", "--file_folder", default=os.path.join(os.path.expanduser("~"),"com_logs"), help="Enter file folder")
    parser.add_argument("-s", "--stdout", action="store_true", default = True, help="Pipe data to stdout")
    parser.add_argument(
        "-e",
        "--exclude_port",
        help="Enter the port name to exclude from monitoring. Separate multiple ports to exclude with a comma without spaces (,). Example: 'COM3,COM7'"
    )
    args = parser.parse_args()
    while True:
        ports = search_for_ports()

        # Print the names of the serial ports that were found
        print_ports(ports)

        # Create the file folder if it doesn't exist
        create_folder_if_missing(args.file_folder)
        
        threads = []
        for port in ports:
            if args.exclude_port and port.device in args.exclude_port.split(','):
                continue
            filename = os.path.join(args.file_folder, "log_" + port.device + ".txt")
            thread = SerialMonitor(port.device, args.baudrate, filename, args.stdout)
            threads.append(thread)
            thread.start()

        for thread in threads:
            thread.join()

        print('\nConnection lost')

        # Sleep for a period before scanning again for available serial ports
        sleep(10)  # You can adjust the interval (in seconds) as needed

if __name__ == "__main__":
        main()