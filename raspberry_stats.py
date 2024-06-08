import time
import psutil
from Adafruit_SSD1306 import SSD1306_128_32
from PIL import Image, ImageDraw, ImageFont
import socket
import subprocess

# Config OLED display
disp = SSD1306_128_32(rst=None)
disp.begin()
disp.clear()
disp.display()

# Make a blank image for the text, initialized to display text in white
width = disp.height  # Intercambiamos ancho y alto para orientación vertical
height = disp.width
image = Image.new('1', (width, height))

# Make a draw object
draw = ImageDraw.Draw(image)

# Load a font in different sizes
font_name = "Hack-Regular.ttf"
font = ImageFont.truetype(font_name,10)
font_medium = ImageFont.truetype(font_name, 12)  # Asegúrate de tener esta fuente disponible
font_large = ImageFont.truetype(font_name, 24)  # Asegúrate de tener esta fuente disponible

# Corrdinates for the CPU donut
circle_x = width // 2
circle_y_cpu = 64
radius_outer = 15
radius_inner = 12

def get_local_ip():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        # No need to connect to any address, only need to get the IP address
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
    except Exception:
        ip = "0.0.0.0"
    finally:
        s.close()
    return ip

def get_cpu_freq():
    # Get the current frequency of the CPU in GHz
    return psutil.cpu_freq().current/1000

def get_power_usage():
    # This function returns the power usage of the Raspberry Pi in Watts using powertop
    try:
        power_usage = subprocess.check_output(["powertop", "-t", "1", "--time=1"]).decode()
        for line in power_usage.split('\n'):
            if "Power est." in line:
                return line.split()[2]
    except Exception as e:
        return "N/A"

def get_cpu_temp():
    try:
        cmd = "vcgencmd measure_temp | awk -F= '{printf \"%s\", $2}'"
        temp = subprocess.check_output(cmd,shell=True)
        return temp.decode()
    except (KeyError, IndexError):
        return "N/A"

while True:
    # Clear the image buffer by drawing a black filled box
    draw.rectangle((0, 0, width, height), outline=0, fill=0)
    
    # Get the system stats
    cpu_usage = psutil.cpu_percent()
    ram_usage = psutil.virtual_memory().percent
    cpu_freq = get_cpu_freq()
    cpu_temp = str(get_cpu_temp()).split(".")[0]
    local_ip = get_local_ip()
    local_ip = str(local_ip).split(".")[-1]
    disk_usage = psutil.disk_usage('/').percent

    # Draw the local IP address at the top of the screen in large font
    draw.text(
        (width // 2 - draw.textsize(local_ip, font=font_large)[0] // 2, 0),
        local_ip, font=font_large, fill=255
    )
    # Draw divisory line -----------------------
    line_y = 28
    draw.line((0, line_y, width, line_y), fill=255)
    draw.line((0, line_y+1, width, line_y+1), fill=255)
    
    # Print cpu frequence
    draw.text(
            (width // 2 - draw.textsize(f"{cpu_freq:.1f}GHz", font=font)[0] // 2, 35),
        f"{cpu_freq:.1f}GHz", font=font, fill=255
    )

    # Draw the CPU donut
    cpu_angle = (cpu_usage / 100) * 360
    draw.pieslice(
        [circle_x - radius_outer, circle_y_cpu - radius_outer, circle_x + radius_outer, circle_y_cpu + radius_outer],
        start=0, end=cpu_angle, fill=255
    )
    draw.pieslice(
        [circle_x - radius_inner, circle_y_cpu - radius_inner, circle_x + radius_inner, circle_y_cpu + radius_inner],
        start=0, end=360, fill=0
    )

    # Draw the CPU usage percentage in the middle of the donut in medium font
    draw.text((width // 2 - draw.textsize(f"{cpu_usage:.0f}", font=font_medium)[0] // 2, 57), f"{cpu_usage:.0f}", font=font_medium, fill=255)
    
    # Draw the stats text in the middle of the screen in medium font
    stats_text = [
        f"R{ram_usage:.0f}%",
        f"D{disk_usage:.0f}%",
        f"{cpu_temp}°C",
    ]
    
    y_offset = 86
    for i, text in enumerate(stats_text):
        draw.text((width // 2 - draw.textsize(text, font=font_medium)[0] // 2, y_offset), text, font=font_medium, fill=255)
        y_offset += 15

    # Rotate the image 90 degrees to display it vertically
    rotated_image = image.rotate(90, expand=1)
    
    # Show the image on the display
    disp.image(rotated_image)
    disp.display()
    time.sleep(1)
   

