use strict;
use warnings;

package RFIDScanner::Request;

sub _hexdump {
    my ($data) = @_;
    my $out = '';
    for (my $i=0; $i<length $data; $i+=16) {
        $out .= sprintf "%3d:", $i;
        for (my $j=$i; $j<$i+16; $j++) {
            if ($j<length $data) {
                $out .= sprintf " %02x", ord(substr($data, $j, 1));
            } else {
                $out .= "   ";
            }
        }
        $out .= " [";
        for (my $j=$i; $j<$i+16 and $j<length $data; $j++) {
            my $h = substr($data, $j, 1);
            $h =~ s{[^\x20-\x7E]}{.}g;
            $out .= $h;
        }
        $out .= "]\n";
    }
    return $out;
}


sub new {
    my ($type, $data) = @_;
    $data = $data->raw_body if UNIVERSAL::isa($data, 'Plack::Request');
    length($data) >= 40 or die "expected 40+ bytes, got: ".length($data); 

    my $dev = join(':', map { sprintf("%02x", ord($_)); } split //, substr($data, 2, 6));
    my $shelf = substr($data, 8, 32);
    $shelf =~ s{\0+$}{};
    $data = substr($data, 40);
    #print STDERR "DEV $dev => shelf: '$shelf'\n";
    
    my $self = bless {
        dev => $dev,
        shelf => $shelf,
        records => [],
    }, $type;

    BLOCK:
    while(length($data)>=9) {
        my $_rfid = substr($data, 0, 8);
        my $rfid = join(':', map { sprintf("%02x", ord($_)); } split //, $_rfid);
        my $flags = ord(substr($data, 8, 1));
        my $len = ord(substr($data, 9, 1));
        my $record_data = substr($data, 10, $len);
        $data = substr($data, 10+$len);
        #print STDERR "$rfid => (f: $flags) $len bytes\n";

        my $record =  {
            rfid => $rfid,
            raw_rfid => $_rfid,
            flags => $flags,
            data => $record_data,
        };
        push @{$self->{records}}, $record;

        if (my $obj = $self->parse_record($record_data)) {
            $record->{$_} //= $obj->{$_} for keys %$obj; 
        }
    }

    return $self;
}

sub _crc {
    my ($data) = @_;

    my $poly = 0x1021;
    my $crc = 0xffff;

    for my $ch (split //, $data) {
        my $c = ord($ch);
        #printf "DBG %d '%s' => ", $c, $ch;
        $c <<=8;
        for (my $i=0; $i<8; $i++) {
            my $xor_f = (($crc^$c)&0x8000) != 0;
            $crc <<= 1;
            $xor_f and $crc = $crc ^ $poly;
            $c <<= 1;
        }
        $crc &= 0xffff;
        #printf "[%04x]\n", $crc;
    }
    return pack('v', $crc);
}


sub parse_record {
    my ($self, $data) = @_;
    if ($data =~ m{^.\0+$}) { # only first byte set => empty
        return {
            type => 'empty',
        };
    }

    if ($data =~ m{^.?(SHELF\#)(?<shelf>.*)}) {
        my $shelf = $+{shelf};
        $shelf =~ s{\0+$}{};
        return {
            type => 'shelf',
            shelf => $+{shelf},
        };
    }
    if ($data =~
        m{^ (?<before>.*?)
            (?<data>
                (?<d1>
                    (?<ver>.) # 4 bit version, 4 bit type (0=>buy, 1=>circ, 2=>no circ, 7=>throw, 8=>patroncard)
                    (?<tot>.)
                    (?<nr>.)
                    (?<barcode>[\d\x00]{16})
                )
                (?<crc>..)
                (?<d2>
                    (?<country>[A-Z][A-Z])
                    (?<library>[\d\x00]{11})
                )
            )(?<after>.*)
        $}x
    ) { 
        my %m = %+;
        my $ver = ord($+{ver});
        my $given_crc = unpack('H*', $+{crc});
        my $calc_crc = unpack('H*', _crc($+{d1}.$+{d2}));
        $given_crc eq $calc_crc or die "invalid crc, got: '$given_crc', expected '$calc_crc'";
        my $library = $m{library}; $library =~ s{\0}{}g;
        my $barcode = $m{barcode}; $barcode =~ s{\0}{}g;
        return {
            type => "item",
                 vertype => $ver,
                 item => [ord($m{nr}),ord($m{tot})],
                 barcode => $barcode,
                 country => $m{country},
                 library => $library,
                 _crc => unpack('H*', $m{crc}),
                 _trim => {
                     before => unpack('H*', $m{before}),
                     after => unpack('H*', $m{after}),
                 },
                 #_data => unpack('H*',$m{data}),
        };
    }

    Dump($data);
    warn "can't parse ".Dumper($data);
    return {};
}

1;
