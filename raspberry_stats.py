import time
import psutil
import socket
import subprocess
from Adafruit_SSD1306 import SSD1306_128_32
from PIL import Image, ImageDraw, ImageFont

class RaspberryPiStatsDisplay:
    def __init__(self):
        # Initialize the OLED display
        self.disp = SSD1306_128_32(rst=None)
        self.disp.begin()
        self.disp.clear()
        self.disp.display()

        # Set dimensions for vertical orientation
        self.width = self.disp.height
        self.height = self.disp.width
        self.image = Image.new('1', (self.width, self.height))
        self.draw = ImageDraw.Draw(self.image)

        # Load fonts
        self.font_name = "Hack-Regular.ttf"
        self.font_small = self.load_font(size=10)
        self.font_medium = self.load_font(size=12)
        self.font_large = self.load_font(size=24)

        # Coordinates and graphic parameters
        self.circle_x = self.width // 2
        self.circle_y_cpu = 64
        self.radius_outer = 15
        self.radius_inner = 12

    def load_font(self, size):
        try:
            return ImageFont.truetype(self.font_name, size)
        except IOError:
            # If the font is not available, use a default font
            return ImageFont.load_default()

    def get_local_ip(self):
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.connect(("8.8.8.8", 80))
            ip = s.getsockname()[0]
        except Exception:
            ip = "0"
        finally:
            s.close()
        return ip.split(".")[-1]

    def get_cpu_freq(self):
        try:
            freq = psutil.cpu_freq().current / 1000
            return f"{freq:.1f}GHz"
        except Exception:
            return "N/A"

    def get_cpu_temp(self):
        try:
            output = subprocess.check_output(["vcgencmd", "measure_temp"]).decode()
            temp = output.strip().replace("temp=", "").replace("'C", "")
            temp = str(temp).split(".")[0]
            return f"{temp}°C"
        except Exception:
            return "N/A"

    def collect_stats(self):
        stats = {
            'cpu_usage': psutil.cpu_percent(interval=None),
            'ram_usage': psutil.virtual_memory().percent,
            'disk_usage': psutil.disk_usage('/').percent,
            'cpu_freq': self.get_cpu_freq(),
            'cpu_temp': self.get_cpu_temp(),
            'local_ip': self.get_local_ip(),
        }
        return stats

    def draw_display(self, stats):
        # Clear the image
        self.draw.rectangle((0, 0, self.width, self.height), outline=0, fill=0)

        # Adjust font size for IP address if necessary
        ajust_y = 0
        max_width = self.width
        font_size = 24  # Start with font_large
        font_ip = self.load_font(font_size)
        text_width, _ = self.draw.textsize(stats['local_ip'], font=font_ip)
        while text_width > max_width and font_size > 8:
            font_size -= 1
            ajust_y += 1
            font_ip = self.load_font(font_size)
            text_width, _ = self.draw.textsize(stats['local_ip'], font=font_ip)

        # Display local IP address
        self.draw_centered_text(stats['local_ip'], y=ajust_y, font=font_ip)

        # Draw dividing line
        line_y = 28
        self.draw.line([(0, line_y), (self.width, line_y)], fill=255)
        self.draw.line([(0, line_y + 1), (self.width, line_y + 1)], fill=255)

        # Display CPU frequency
        self.draw_centered_text(stats['cpu_freq'], y=32, font=self.font_small)

        # Draw CPU usage donut
        cpu_angle = (stats['cpu_usage'] / 100) * 360
        self.draw.pieslice(
            [self.circle_x - self.radius_outer, self.circle_y_cpu - self.radius_outer,
             self.circle_x + self.radius_outer, self.circle_y_cpu + self.radius_outer],
            start=0, end=cpu_angle, fill=255
        )
        self.draw.pieslice(
            [self.circle_x - self.radius_inner, self.circle_y_cpu - self.radius_inner,
             self.circle_x + self.radius_inner, self.circle_y_cpu + self.radius_inner],
            start=0, end=360, fill=0
        )

        # Display CPU usage percentage in the center of the donut
        cpu_usage_text = f"{stats['cpu_usage']:.0f}%"
        self.draw_centered_text(cpu_usage_text, y=58, font=self.font_small)

        # Display other stats
        stats_texts = [
            f"R:{stats['ram_usage']:.0f}%",
            f"D:{stats['disk_usage']:.0f}%",
            f"{stats['cpu_temp']}",
        ]

        y_offset = 86
        for text in stats_texts:
            self.draw_centered_text(text, y=y_offset, font=self.font_small)
            y_offset += 15

        # Rotate and display the image on the display
        rotated_image = self.image.rotate(90, expand=1)
        self.disp.image(rotated_image)
        self.disp.display()

    def draw_centered_text(self, text, y, font):
        text_width, _ = self.draw.textsize(text, font=font)
        x = (self.width - text_width) // 2
        self.draw.text((x, y), text, font=font, fill=255)

    def run(self):
        while True:
            stats = self.collect_stats()
            self.draw_display(stats)
            time.sleep(1)

if __name__ == "__main__":
    display = RaspberryPiStatsDisplay()
    display.run()
