typeswitch ("foo")
  case $a as xs:integer return $a + 1
  case $b as xs:string  return ($b, 7)
  default $b return 42 + $b
