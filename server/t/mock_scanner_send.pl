use strict;
use warnings;
use Data::Dumper;

use lib 'lib/';
#use Spore::Utils;
use LWP::UserAgent;
use HTTP::Request;

my $ua = LWP::UserAgent->new();

my $package = <<'EOD';
  0: 42 42 12 34 56 78 9A BC  // MAGIC + MAC_ADDR

  0: 00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  // SHELF
  0: 00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00

  0: E0 11 22 33 44 55 66 77  00 24                    // RFID ID + FLAG + LEN
  0: 53 48 45 4C 46 23 68 75  74 6C 2E 73 74 61 63 6B  "SHELF#hutl.stack"
 10: 2E 66 6F 6F 00 00 00 00  00 00 00 00 00 00 00 00  ".foo............"
 20: 00 00 00 00

  0: E0 04 01 50 5A 89 BD E3  00 24
  0: 11 02 02 33 30 31 31 39  31 31 36 38 31 38 37 34  "...3011911681874"
 10: 39 00 00 95 F1 4E 4F 30  32 30 33 30 30 30 30 00  "9..8.NO02030000."
 20: 00 00 00 00                                       "...."


EOD

my $raw = '';

for (split /\n/, $package) {
    next unless /\w/;
    m{0: ++(( *([a-fA-F0-9]{2}))++)} or die "invalid line: '$_'";
    for (split / +/, $1) {
        $raw .= chr(hex($_));
    }
}
#print STDERR Spore::Utils::hexdump($raw);
my $req = HTTP::Request->new(POST => 'http://127.0.0.1:8081/api/spore/hub/scanner', ["Content-Type" => "application/octet-stream", "Accept" => "application/octet-stream"], $raw);
my $res = $ua->request($req);

print Dumper($res->content);
