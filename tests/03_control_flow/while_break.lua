a = 0
while a < 10 do
    if a == 5 then
        break
    end
    print (a)
    a = a + 1
end

print ("Broken")

while a < 10 do 
    print ("In loop " .. a)
    a = a + 1
end
